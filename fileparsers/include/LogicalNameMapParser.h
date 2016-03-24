/*
 * LogicalNameMap.h
 *
 *  Created on: Feb 8, 2016
 *      Author: Martin Hierholzer
 */

#ifndef MTCA4U_LOGICAL_NAME_MAP_H
#define MTCA4U_LOGICAL_NAME_MAP_H

#include <map>
#include <unordered_set>
#include <boost/shared_ptr.hpp>

#include "RegisterCatalogue.h"
#include "DeviceBackend.h"
#include "RegisterPlugin.h"
#include "RegisterInfo.h"
#include "RegisterPath.h"
#include "DynamicValue.h"

// forward declaration
namespace xmlpp {
  class Node;
  class Element;
}

namespace mtca4u {

  class LogicalNameMappingBackend;

  /** Logical name map: store information from xlmap file and provide it to the LogicalNameMappingBackend and
   *  its register accessors. */
  class LogicalNameMapParser {

    public:

      /** Potential target types */
      enum TargetType { INVALID, REGISTER, RANGE, CHANNEL, INT_CONSTANT, INT_VARIABLE };

      /** Sub-class: single entry of the logical name mapping */
      class RegisterInfo : public mtca4u::RegisterInfo {
        public:

          virtual RegisterPath getRegisterName() const {
            return name;
          }

          virtual unsigned int getNumberOfElements() const {
            return length;
          }

          virtual unsigned int getNumberOfDimensions() const {
            return 1;
          }

          virtual unsigned int getNumberOfChannels() const {
            return 1;
          }

          /** Name of the registrer */
          RegisterPath name;

          /** Type of the target */
          TargetType targetType;

          /** The target device alias */
          DynamicValue<std::string> deviceName;

          /** The target register name */
          DynamicValue<std::string> registerName;

          /** The first index in the range */
          DynamicValue<unsigned int> firstIndex;

          /** The length of the range (i.e. number of indices) */
          DynamicValue<unsigned int> length;

          /** The channel of the target 2D register */
          DynamicValue<unsigned int> channel;

          /** The constant integer value */
          DynamicValue<int> value;

          /** test if deviceName is set (depending on the targetType) */
          bool hasDeviceName() const {
            return targetType != TargetType::INT_CONSTANT && targetType != TargetType::INT_VARIABLE;
          }

          /** test if registerName is set (depending on the targetType) */
          bool hasRegisterName() const {
            return targetType != TargetType::INT_CONSTANT && targetType != TargetType::INT_VARIABLE;
          }

          /** test if firstIndex is set (depending on the targetType) */
          bool hasFirstIndex() const {
            return targetType == TargetType::RANGE;
          }

          /** test if length is set (depending on the targetType) */
          bool hasLength() const {
            return targetType == TargetType::RANGE;
          }

          /** test if channel is set (depending on the targetType) */
          bool hasChannel() const {
            return targetType == TargetType::CHANNEL;
          }

          /** test if value is set (depending on the targetType) */
          bool hasValue() const {
            return targetType == TargetType::INT_CONSTANT || targetType == TargetType::INT_VARIABLE;
          }

          /** create the internal register accessors */
          void createInternalAccessors(boost::shared_ptr<DeviceBackend> &backend) {
            deviceName.createInternalAccessors(backend);
            registerName.createInternalAccessors(backend);
            firstIndex.createInternalAccessors(backend);
            length.createInternalAccessors(backend);
            channel.createInternalAccessors(backend);
            value.createInternalAccessors(backend);
          }

          /** constuctor: initialise values */
          RegisterInfo()
          : targetType(TargetType::INVALID)
          {}

        protected:

          friend class LogicalNameMapParser;
      };

      /** Constructor: parse map from XML file */
      LogicalNameMapParser(const std::string &fileName) {
        parseFile(fileName);
      }

      /** Default constructor: Create empty map. */
      LogicalNameMapParser() {
      }

      /** Obtain register information for the named register. The register information can be updated, which *will*
       *  have an effect on the logical map itself (and thus any other user of the same register info)! */
      boost::shared_ptr<RegisterInfo> getRegisterInfoShared(const std::string &name);

      /** Obtain register information for the named register (const version) */
      const RegisterInfo& getRegisterInfo(const std::string &name) const;

      /** Obtain list of all target devices referenced in the map */
      std::unordered_set<std::string> getTargetDevices() const;

    protected:

      /** file name of the logical map */
      std::string _fileName;

      /** current register path in the map */
      RegisterPath currentModule;

      /** actual register info map (register name to target information) */
      //std::map< std::string, boost::shared_ptr<RegisterInfo> > _map;
      RegisterCatalogue _catalogue;

      /** parse the given XML file */
      void parseFile(const std::string &fileName);

      /** called inside parseFile() to parse an XML element and its sub-elements recursivly */
      void parseElement(RegisterPath currentPath, const xmlpp::Element *element);

      /** throw a parsing error with more information */
      void parsingError(const std::string &message);

      /** Build a Value object for a given subnode. */
      template<typename ValueType>
      DynamicValue<ValueType> getValueFromXmlSubnode(const xmlpp::Node *node, const std::string &subnodeName);

      friend class LogicalNameMappingBackend;

  };

} // namespace mtca4u

#endif /* MTCA4U_LOGICAL_NAME_MAP_H */