#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

#include "Exception.h"
#include "MapFileParser.h"
#include "NumericAddressedBackendMuxedRegisterAccessor.h" // for the MULTIPLEXED_SEQUENCE_PREFIX constant

namespace ChimeraTK {

  RegisterInfoMapPointer MapFileParser::parse(const std::string& file_name) {
    std::ifstream file;
    std::string line;
    std::istringstream is;
    uint32_t line_nr = 0;

    file.open(file_name.c_str());
    if(!file) {
      throw ChimeraTK::logic_error("Cannot open file \"" + file_name + "\"");
    }
    RegisterInfoMapPointer pmap(new RegisterInfoMap(file_name));
    std::string name;        /**< Name of register */
    uint32_t nElements;      /**< Number of elements in register */
    uint64_t address;        /**< Relative address in bytes from beginning  of the
                                bar(Base Address Range)*/
    uint32_t nBytes;         /**< Size of register expressed in bytes */
    uint64_t bar;            /**< Number of bar with register */
    uint32_t width;          /**< Number of significant bits in the register */
    int32_t nFractionalBits; /**< Number of fractional bits */
    bool signedFlag;         /**< Signed/Unsigned flag */
    RegisterInfoMap::RegisterInfo::Access registerAccess;
    RegisterInfoMap::RegisterInfo::Type type;

    std::string module; /**< Name of the module this register is in*/

    while(std::getline(file, line)) {
      bool failed = false;
      line_nr++;
      // Remove whitespace from beginning of line
      line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](int c) { return !isspace(c); }));
      if(!line.size()) {
        continue;
      }
      if(line[0] == '#') {
        continue;
      }
      if(line[0] == '@') {
        std::string org_line = line;
        RegisterInfoMap::MetaData md;
        // Remove the '@' character...
        line.erase(line.begin(), line.begin() + 1);
        // ... and remove all the whitespace after it
        line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](int c) { return !isspace(c); }));
        is.str(line);
        is >> md.name;
        if(!is) {
          throw ChimeraTK::logic_error(
              "Parsing error in map file '" + file_name + "' on line " + std::to_string(line_nr));
        }
        line.erase(line.begin(), line.begin() + md.name.length());
        line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](int c) { return !isspace(c); }));
        md.value = line;
        pmap->insert(md);
        is.clear();
        continue;
      }
      is.str(line);

      std::string moduleAndRegisterName;
      is >> moduleAndRegisterName;

      std::pair<std::string, std::string> moduleAndNamePair = splitStringAtLastDot(moduleAndRegisterName);
      module = moduleAndNamePair.first;
      name = moduleAndNamePair.second;
      if(name.empty()) {
        throw ChimeraTK::logic_error("Parsing error in map file '" + file_name + "' on line " +
            std::to_string(line_nr) +
            ": "
            "empty register name");
      }

      is >> std::setbase(0) >> nElements >> std::setbase(0) >> address >> std::setbase(0) >> nBytes;
      if(!is) {
        throw ChimeraTK::logic_error(
            "Parsing error in map file '" + file_name + "' on line " + std::to_string(line_nr));
      }
      // first, set default values for 'optional' fields
      bar = 0x0;
      width = 32;
      nFractionalBits = 0;
      signedFlag = true;
      registerAccess = RegisterInfoMap::RegisterInfo::Access::READWRITE;
      type = RegisterInfoMap::RegisterInfo::Type::FIXED_POINT;

      is >> std::setbase(0) >> bar;
      if(is.fail()) {
        failed = true;
      }
      if(!failed) {
        is >> std::setbase(0) >> width;
        if(is.fail()) {
          failed = true;
        }
        else {
          if(width > 32) {
            throw ChimeraTK::logic_error("Parsing error in map file '" + file_name + "' on line " +
                std::to_string(line_nr) +
                ": "
                "register width too big");
          }
        }
      }
      if(!failed) {
        std::string bitInterpretation;
        is >> bitInterpretation;
        if(is.fail()) {
          failed = true;
        }
        else {
          // factored out because rather lengthy
          auto type_and_nFractionBits = getTypeAndNFractionalBits(bitInterpretation);
          type = type_and_nFractionBits.first;
          nFractionalBits = type_and_nFractionBits.second;

          if(nFractionalBits > 1023 || nFractionalBits < -1024) {
            throw ChimeraTK::logic_error("Parsing error in map file '" + file_name + "' on line " +
                std::to_string(line_nr) +
                ": "
                "too many fractional bits");
          }
        }
      }

      if(!failed) {
        is >> std::setbase(0) >> signedFlag;
        if(is.fail()) {
          failed = true;
        }
      }

      if(!failed) {
        std::string accessString;
        is >> accessString;
        if(is.fail()) {
          failed = true;
        }
        else {
          if(accessString == "RO")
            registerAccess = RegisterInfoMap::RegisterInfo::Access::READ;
          else if(accessString == "RW")
            registerAccess = RegisterInfoMap::RegisterInfo::Access::READWRITE;
          else if(accessString == "WO")
            registerAccess = RegisterInfoMap::RegisterInfo::Access::WRITE;
          else
            throw ChimeraTK::logic_error("Parsing error in map file '" + file_name + "' on line " +
                std::to_string(line_nr) +
                ": "
                "invalid data access");
        }
      }
      is.clear();

      RegisterInfoMap::RegisterInfo registerInfo(name, nElements, address, nBytes, bar, width, nFractionalBits,
          signedFlag, module, 1, false, registerAccess, type);
      pmap->insert(registerInfo);
    }

    // search for 2D registers and add 2D entries
    std::vector<RegisterInfoMap::RegisterInfo> newInfos;
    for(auto& info : *pmap) {
      // check if 2D register, otherwise ignore
      if(info.name.substr(0, MULTIPLEXED_SEQUENCE_PREFIX.length()) != MULTIPLEXED_SEQUENCE_PREFIX) continue;
      // name of the 2D register is the name without the sequence prefix
      name = info.name.substr(MULTIPLEXED_SEQUENCE_PREFIX.length());
      // count number of channels and number of entries per channel
      size_t nChannels = 0;
      size_t nBytesPerEntry = 0; // nb. of bytes per entry for all channels together
      // We have to aggregate the fractional/ signed information of all cannels.
      // Afterwards we set fractional to 9999 (way out of range, max allowed is
      // 1023) if there are fractional bits, just to indicate that the register is
      // not integer and probably a floating point accessor should be used (e.g.
      // in QtHardMon).
      bool isSigned = false;
      bool isInteger = true;
      uint32_t maxWidth = 0;
      auto cat = pmap->getRegisterCatalogue();
      while(cat.hasRegister(RegisterPath(info.module) / (SEQUENCE_PREFIX + name + "_" + std::to_string(nChannels)))) {
        RegisterInfoMap::RegisterInfo subInfo;
        pmap->getRegisterInfo(
            RegisterPath(info.module) / (SEQUENCE_PREFIX + name + "_" + std::to_string(nChannels)), subInfo);
        nBytesPerEntry += subInfo.nBytes;
        nChannels++;
        if(subInfo.signedFlag) {
          isSigned = true;
        }
        if(subInfo.nFractionalBits > 0) {
          isInteger = false;
        }
        maxWidth = std::max(maxWidth, subInfo.width);
      }
      if(nChannels == 0) continue;
      // Compute number of elements. Note that there may be additional padding bytes specified in the map file. The
      // integer division is then rounding down, so it may be that nElements * nBytesPerEntry != info.nBytes.
      nElements = info.nBytes / nBytesPerEntry;
      // add it to the map
      RegisterInfoMap::RegisterInfo newEntry(name, nElements, info.address, nElements * nBytesPerEntry, info.bar,
          maxWidth, (isInteger ? 0 : 9999) /*fractional bits*/, isSigned, info.module, nChannels, true,
          info.registerAccess);
      newInfos.push_back(newEntry);
    }
    // insert the new entries to the catalogue
    for(auto& entry : newInfos) {
      pmap->insert(entry);
    }

    return pmap;
  }

  std::pair<std::string, std::string> MapFileParser::splitStringAtLastDot(std::string moduleDotName) {
    size_t lastDotPosition = moduleDotName.rfind('.');

    // some special case handlings to avoid string::split from throwing exceptions
    if(lastDotPosition == std::string::npos) {
      // no dot found, the whole string is the second argument
      return std::make_pair(std::string(), moduleDotName);
    }

    if(lastDotPosition == 0) {
      // the first character is a dot, everything from pos 1 is the second
      // argument
      if(moduleDotName.size() == 1) {
        // it's just a dot, return  two empty strings
        return std::make_pair(std::string(), std::string());
      }

      // split after the first character
      return std::make_pair(std::string(), moduleDotName.substr(1));
    }

    if(lastDotPosition == (moduleDotName.size() - 1)) {
      // the last character is a dot. The second argument is empty
      return std::make_pair(moduleDotName.substr(0, lastDotPosition), std::string());
    }

    return std::make_pair(moduleDotName.substr(0, lastDotPosition), moduleDotName.substr(lastDotPosition + 1));
  }

  std::pair<RegisterInfoMap::RegisterInfo::Type, int> MapFileParser::getTypeAndNFractionalBits(
      std::string bitInterpretation) {
    if(bitInterpretation == "IEEE754") {
      return {RegisterInfoMap::RegisterInfo::Type::IEEE754, 0};
    }
    else if(bitInterpretation == "ASCII") {
      return {RegisterInfoMap::RegisterInfo::Type::ASCII, 0};
    }
    else {
      // If it is a digit the implicit interpretation is FixedPoint
      try {
        int nBits = std::stoi(bitInterpretation, nullptr,
            0); // base 0 = auto, hex or dec or oct
        return {RegisterInfoMap::RegisterInfo::Type::FIXED_POINT, nBits};
      }
      catch(std::exception& e) {
        throw ChimeraTK::logic_error(std::string("Map file error in bitInterpretation: wrong argument '") +
            bitInterpretation + "', caught exception: " + e.what());
      }
    }
  }

} // namespace ChimeraTK
