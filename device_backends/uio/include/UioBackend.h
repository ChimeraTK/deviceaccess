#ifndef CHIMERA_TK_UIO_BACKEND_H 
#define CHIMERA_TK_UIO_BACKEND_H

#include <stdint.h>
#include <stdlib.h>
#include <ChimeraTK/cppext/future_queue.hpp>

#include "NumericAddressedBackend.h"
#include "NumericAddressedBackendRegisterAccessor.h"

namespace ChimeraTK {

        // we need a non-templated base class
  struct InterruptWaitingAccessor{
          virtual void send() = 0;
  };
  
  template<typename UserType> 
  struct InterruptWaitingAccessor_impl : public NumericAddressedBackendRegisterAccessor<UserType,FixedPointConverter,true>, public InterruptWaitingAccessor{                
        cppext::future_queue<UserType> myQueue;
        UserType buffer;

        InterruptWaitingAccessor_impl(boost::shared_ptr<DeviceBackend> dev, const RegisterPath& registerPathName,
        size_t numberOfWords, size_t wordOffsetInRegister, AccessModeFlags flags) : 
        NumericAddressedBackendRegisterAccessor<UserType,FixedPointConverter,true>(dev, registerPathName, numberOfWords, wordOffsetInRegister, flags) {};
        
        void send() override {
            myQueue.push(UserType());
        }

        void doReadTransfer() override {
            myQueue.pop_wait(buffer);
        }
        void doPostRead() override {
        //       buffer_2D[0][0] = buffer;
        }
  };
  

  /** A class to provide the uio device functionality."
   *
   */
  class UioBackend : public NumericAddressedBackend {
   private:
    int _deviceID;
    void * _deviceMemBase;
    size_t _deviceMemSize;
    std::string _deviceName;
    std::string _deviceNodeName;
    std::string _deviceSysfsPathName;
    
    void UioFindByName();
    void UioMMap();
    void UioUnmap();

    std::thread _interruptWaitingThread;
    // 32 lists of accessors
    std::map< int, std::list< boost::shared_ptr<InterruptWaitingAccessor> > > accessorLists;
    // do we need this?
    //std::atomic<bool> stopInnterruptLoop = {false};
    void interruptWaitingLoop();
    

    /** constructor called through createInstance to create device object */

   public:
    UioBackend(std::string deviceName, std::string mapFileName = "");
    virtual ~UioBackend();
    
    void open() override;
    void close() override;

    void read(uint8_t bar, uint32_t address, int32_t* data, size_t sizeInBytes) override;
    void write(uint8_t bar, uint32_t address, int32_t const* data, size_t sizeInBytes) override;

    std::string readDeviceInfo() override;
    
    std::string createErrorStringWithErrnoText(std::string const& startText);

    /*Host or parameters (at least for now) are just place holders as uiodevice
     * does not use them*/
    static boost::shared_ptr<DeviceBackend> createInstance(std::string address,
        std::map<std::string, std::string>
            parameters);

    template<typename UserType>
    boost::shared_ptr<NDRegisterAccessor<UserType>> getRegisterAccessor_impl(
      const RegisterPath& registerPathName, size_t numberOfWords, size_t wordOffsetInRegister, AccessModeFlags flags);
    
    template<typename UserType>
    boost::shared_ptr<NDRegisterAccessor<UserType>> getInterruptWaitingAccessor(
        const RegisterPath& registerPathName, size_t numberOfWords, size_t wordOffsetInRegister, AccessModeFlags flags);
 
  };

  
} // namespace ChimeraTK
#endif /* CHIMERA_TK_UIO_BACKEND_H */
