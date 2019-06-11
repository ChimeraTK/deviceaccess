#include <errno.h>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fstream>
#include <sys/mman.h>
#include <pthread.h>

//#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <sys/poll.h>

// the io constants and struct for the driver
// FIXME: they should come from the installed driver
#include "UioBackend.h"

namespace ChimeraTK {

    UioBackend::UioBackend(std::string deviceName, size_t memSize, std::string mapFileName) : NumericAddressedBackend(mapFileName),
    _deviceID(0), _deviceMemBase(nullptr), _deviceMemSize(memSize), _deviceNodeName(std::string("/dev/" + deviceName)) {
        FILL_VIRTUAL_FUNCTION_TEMPLATE_VTABLE_STANDALONE(getRegisterAccessor_impl, 4);
    }

    UioBackend::~UioBackend() {
        close();
    }

    void UioBackend::UioMMap() {
        if (MAP_FAILED == (_deviceMemBase = mmap(NULL, _deviceMemSize, PROT_READ | PROT_WRITE, MAP_SHARED, _deviceID, 0))) {
            ::close(_deviceID);
            throw ChimeraTK::runtime_error(createErrorStringWithErrnoText("Cannot allocate Memory: "));
        }
        return;
    }

    void UioBackend::UioUnmap() {
        munmap(_deviceMemBase, _deviceMemSize);
    }

    void UioBackend::open() {
#ifdef _DEBUG
        std::cout << "open uio dev" << std::endl;
#endif
        if (_opened) {
            throw ChimeraTK::logic_error("Device already has been opened");
        }

        _deviceID = ::open(_deviceNodeName.c_str(), O_RDWR);
        if (_deviceID < 0) {
            throw ChimeraTK::runtime_error(createErrorStringWithErrnoText("Cannot open device: "));
        }

        UioMMap();
#ifdef _DEBUG
        std::cout << "uio mmaped " << std::endl;
#endif
        _opened = true;
        
        if (false == _catalogue.hasRegister("INTERRUPT_WORD")) {
            return;
        }
        
        _interruptWaitingThread = std::thread(&UioBackend::interruptWaitingLoop, this);
        
        //if user has root privilege, increase priority of interruptWaitingThread
        if (0 == geteuid()) {
            struct sched_param param;
            param.sched_priority = 11;
        
            if (pthread_setschedparam(_interruptWaitingThread.native_handle(), SCHED_FIFO, &param)) {
                throw ChimeraTK::runtime_error(createErrorStringWithErrnoText("Cannot set interruptWaitingThread priority: "));
            }
        }
    }

    void UioBackend::close() {
        if (_opened) {
            _stopInnterruptLoop = true;
            _interruptWaitingThread.join();
            UioUnmap();
            ::close(_deviceID);
        }
        _opened = false;
    }

    void UioBackend::read(uint8_t bar, uint32_t address, int32_t* data, size_t sizeInBytes) {
        if (_opened == false) {
            throw ChimeraTK::logic_error("Device closed");
        }
        if (address + sizeInBytes > _deviceMemSize) {
            throw ChimeraTK::logic_error("Read request exceed Device Memory Region");
        }
        std::memcpy(data, _deviceMemBase + address, sizeInBytes);
    }

    void UioBackend::write(uint8_t bar, uint32_t address, int32_t const* data, size_t sizeInBytes) {
        if (_opened == false) {
            throw ChimeraTK::logic_error("Device closed");
        }
        if (address + sizeInBytes > _deviceMemSize) {
            throw ChimeraTK::logic_error("Read request exceed Device Memory Region");
        }
        std::memcpy(_deviceMemBase + address, data, sizeInBytes);
    }

    std::string UioBackend::readDeviceInfo() {
        if (!_opened) throw ChimeraTK::logic_error("Device not opened.");
        std::ostringstream os;
        os << "Uio Device: " << _deviceNodeName;
        return os.str();
    }

    std::string UioBackend::createErrorStringWithErrnoText(std::string const& startText) {
        char errorBuffer[255];
        return startText + _deviceNodeName + ": " + strerror_r(errno, errorBuffer, sizeof (errorBuffer));
    }

    boost::shared_ptr<DeviceBackend> UioBackend::createInstance(std::string address,
            std::map<std::string, std::string>
            parameters) {
        if (address.size() == 0) {
            throw ChimeraTK::logic_error("Device Name not specified.");
        }
        if (parameters["memSize"].size() == 0) {
            throw ChimeraTK::logic_error("Device Memory Size not specified.");
        }       

        return boost::shared_ptr<DeviceBackend>(new UioBackend(address, std::stoul(parameters["memSize"]), parameters["map"]));
    }

    void UioBackend::interruptWaitingLoop() {
        int32_t interruptWord;   
        
        boost::shared_ptr<RegisterInfo> info = getRegisterInfo("INTERRUPT_WORD");
        auto registerInfo = boost::static_pointer_cast<RegisterInfoMap::RegisterInfo>(info);                
        uint32_t interruptWordAddress = registerInfo->address;
        
        struct pollfd poll_fds[1];
        poll_fds[0].fd = _deviceID;
        poll_fds[0].events = POLLIN;

        while (_stopInnterruptLoop == false) {// only works with detached thread                                        
            int ret = ::poll(poll_fds, 1, 100); //100 ms timeout
            if ((0 < ret) && (poll_fds[0].revents && POLLIN)){
                int dummy;
                ::read(_deviceID, &dummy, sizeof(int));

                read(0 /*bar*/, interruptWordAddress, &interruptWord, sizeof (int32_t));

                //clear the interrupt/s
                write(0 /*bar*/, interruptWordAddress, &interruptWord, sizeof (int32_t)); 
                            
                if (!_accessorLists.empty()) {                
                    for (auto & accessorList : _accessorLists) {
                        int i = accessorList.first;
                        uint32_t iMask = (1 << i);
                        if (iMask & interruptWord & !accessorList.second.empty()) {
                            for (auto & accessor : accessorList.second) {
                                accessor->send();
                            }
                        }
                    }                
                }
            }
        }
#ifdef _DEBUG
        std::cout << "interruptWaitingThread end" << std::endl;
#endif
        
    }

    template<typename UserType>
    boost::shared_ptr<NDRegisterAccessor<UserType>> UioBackend::getRegisterAccessor_impl(
            const RegisterPath& registerPathName, size_t numberOfWords, size_t wordOffsetInRegister, AccessModeFlags flags) {
        if (registerPathName.startsWith("INTERRUPT/")) 
        {
            return getInterruptWaitingAccessor<UserType>(registerPathName, numberOfWords, wordOffsetInRegister, flags);
        } 
        else 
        {  
            return NumericAddressedBackend::getRegisterAccessor_impl<UserType>(registerPathName, numberOfWords, wordOffsetInRegister, flags);
        }
    }

    template<typename UserType>
    boost::shared_ptr<NDRegisterAccessor<UserType>> UioBackend::getInterruptWaitingAccessor(
            const RegisterPath& registerPathName, size_t numberOfWords, size_t wordOffsetInRegister, AccessModeFlags flags) {
        boost::shared_ptr<NDRegisterAccessor < UserType>> accessor;

                boost::shared_ptr<RegisterInfo> info = getRegisterInfo(registerPathName);
                auto registerInfo = boost::static_pointer_cast<RegisterInfoMap::RegisterInfo>(info);  
                                
                if ((registerInfo->registerAccess < RegisterInfoMap::RegisterInfo::Access::I0) || 
                        (registerInfo->registerAccess > RegisterInfoMap::RegisterInfo::Access::I31)) {
                    throw ChimeraTK::logic_error("Not an interrupt Register");
                }
                
                // determine index from RegisterPath
                int interruptNum = ((registerInfo->registerAccess) >> 2) - 1;

                accessor = boost::shared_ptr<NDRegisterAccessor < UserType >> 
                        (new InterruptWaitingAccessor_impl<UserType>(interruptNum, boost::dynamic_pointer_cast<UioBackend>(shared_from_this()), registerPathName, numberOfWords, wordOffsetInRegister, flags));
                
        return accessor;
    }


} // namespace ChimeraTK
