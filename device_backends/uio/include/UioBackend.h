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
    

  /** A class to provide the uio device functionality."
   *
   */
  class UioBackend : public NumericAddressedBackend {
   private:
    int _deviceID;
    void * _deviceMemBase;
    size_t _deviceMemSize;
    std::string _deviceNodeName;
    
    void UioFindByName();
    void UioMMap();
    void UioUnmap();

    std::thread _interruptWaitingThread;
    // 32 lists of accessors
    std::map< int, std::list<InterruptWaitingAccessor *> > _accessorLists;
    
    // do we need this?
    std::atomic<bool> _stopInnterruptLoop = {false};
    void interruptWaitingLoop();
    
    template<typename UserType> friend struct InterruptWaitingAccessor_impl;
    

    /** constructor called through createInstance to create device object */

   public:
    UioBackend(std::string deviceName, size_t memSize, std::string mapFileName = "");
    virtual ~UioBackend();
        
    void open() override;
    void close() override;

    void read(uint8_t bar, uint32_t address, int32_t* data, size_t sizeInBytes) override;
    void write(uint8_t bar, uint32_t address, int32_t const* data, size_t sizeInBytes) override;
    
    std::string readDeviceInfo() override;
    
    std::string createErrorStringWithErrnoText(std::string const& startText);

    static boost::shared_ptr<DeviceBackend> createInstance(std::string address,
        std::map<std::string, std::string>
            parameters);

    template<typename UserType>
    boost::shared_ptr<NDRegisterAccessor<UserType>> getRegisterAccessor_impl(
      const RegisterPath& registerPathName, size_t numberOfWords, size_t wordOffsetInRegister, AccessModeFlags flags);
    
    template<typename UserType>
    boost::shared_ptr<NDRegisterAccessor<UserType>> getInterruptWaitingAccessor(
        int interruptNum, const RegisterPath& registerPathName, size_t numberOfWords, size_t wordOffsetInRegister, AccessModeFlags flags);

   protected:
    void removeAccessor(int interruptNr, InterruptWaitingAccessor * accessor){
            _accessorLists[interruptNr].remove(accessor);
    }
    void addAccessor(int interruptNr, InterruptWaitingAccessor * accessor){
            _accessorLists[interruptNr].push_back(accessor);
    }
    
    template<typename UserType> friend class InterruptWaitingAccessor_impl;


  };
  
  template<typename UserType> 
  class InterruptWaitingAccessor_impl : public NumericAddressedBackendRegisterAccessor<UserType,FixedPointConverter,true>, public InterruptWaitingAccessor{                
  public:
  InterruptWaitingAccessor_impl(
        size_t interruptNum, boost::shared_ptr<UioBackend> dev, const RegisterPath& registerPathName,
        size_t numberOfWords, size_t wordOffsetInRegister, AccessModeFlags flags): 
        NumericAddressedBackendRegisterAccessor<UserType,FixedPointConverter,true>(dev, registerPathName, numberOfWords, wordOffsetInRegister, flags),
                _backend(dev), _interruptNum(interruptNum) {
                _myQueue = cppext::future_queue<UserType>(1);
                _backend->addAccessor(_interruptNum,this);
        }
        
        ~InterruptWaitingAccessor_impl() {
            _backend->removeAccessor(_interruptNum, this);
        }
        
        void send() override {
            _myQueue.push(UserType());
        }

        void doReadTransfer() override {
            _myQueue.pop_wait(_buffer);
        }
        void doPostRead() override {
            NDRegisterAccessor<UserType>::buffer_2D[0][0] = _buffer;
        }

        boost::shared_ptr<UioBackend> _backend;
        cppext::future_queue<UserType> _myQueue;
        UserType _buffer;
        size_t _interruptNum;
        
  };
  
} // namespace ChimeraTK
#endif /* CHIMERA_TK_UIO_BACKEND_H */
