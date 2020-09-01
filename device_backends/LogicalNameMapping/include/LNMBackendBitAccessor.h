/*
 * LNMBackendBufferingChannelAccessor.h
 *
 *  Created on: Feb 15, 2016
 *      Author: Martin Hierholzer
 */

#ifndef CHIMERA_TK_LNM_BACKEND_BIT_ACCESSOR_H
#define CHIMERA_TK_LNM_BACKEND_BIT_ACCESSOR_H

#include <algorithm>

#include <ChimeraTK/cppext/finally.hpp>

#include "Device.h"
#include "FixedPointConverter.h"
#include "LogicalNameMappingBackend.h"
#include "NDRegisterAccessor.h"
#include "TwoDRegisterAccessor.h"

namespace ChimeraTK {

  /*********************************************************************************************************************/

  template<typename UserType>
  class LNMBackendBitAccessor : public NDRegisterAccessor<UserType> {
   public:
    LNMBackendBitAccessor(boost::shared_ptr<DeviceBackend> dev, const RegisterPath& registerPathName,
        size_t numberOfWords, size_t wordOffsetInRegister, AccessModeFlags flags)
    : NDRegisterAccessor<UserType>(registerPathName, flags), _registerPathName(registerPathName),
      _fixedPointConverter(registerPathName, 32, 0, 1) {
      // check for unknown flags
      flags.checkForUnknownFlags({AccessMode::raw});
      // check for illegal parameter combinations
      if(flags.has(AccessMode::raw)) {
        throw ChimeraTK::logic_error("LNMBackendBitAccessor: raw access not supported!");
      }
      _dev = boost::dynamic_pointer_cast<LogicalNameMappingBackend>(dev);
      // copy the register info and create the internal accessors, if needed
      auto info = boost::static_pointer_cast<LNMBackendRegisterInfo>(
          _dev->getRegisterCatalogue().getRegister(_registerPathName));
      // check for incorrect usage of this accessor
      if(info->targetType != LNMBackendRegisterInfo::TargetType::BIT) {
        throw ChimeraTK::logic_error(
            "LNMBackendBitAccessor used for wrong register type."); // LCOV_EXCL_LINE (impossible to test...)
      }
      if(wordOffsetInRegister != 0) {
        throw ChimeraTK::logic_error("LNMBackendBitAccessors cannot have a word offset.");
      }
      if(numberOfWords > 1) {
        throw ChimeraTK::logic_error("LNMBackendBitAccessors must have size 1.");
        // The case that the target size actually is 1 if numberOfWords == 0 cannnot be checked here.
        // 0 is allowed. It is tested after the target has created the accessor with 0 as length parameter.
      }

      // get target device and accessor
      std::string devName = info->deviceName;
      boost::shared_ptr<DeviceBackend> targetDevice;
      if(devName != "this") {
        targetDevice = _dev->_devices[devName];
      }
      else {
        targetDevice = dev;
      }
      auto& map = boost::fusion::at_key<uint64_t>(_dev->sharedAccessorMap.table);
      auto it = map.find(RegisterPath(info->registerName));
      // Obtain accessor if not found in the map or if weak pointer has expired
      // Note: we must not use boost::weak_ptr::expired() here, because we have to check the status and obtain the
      // accessor in one atomic step.
      if(it == map.end() || (_accessor = map[RegisterPath(info->registerName)].accessor.lock()) == nullptr) {
        _accessor = targetDevice->getRegisterAccessor<uint64_t>(
            RegisterPath(info->registerName), numberOfWords, wordOffsetInRegister, {});
        if(_accessor->getNumberOfSamples() != 1) {
          throw ChimeraTK::logic_error("LNMBackendBitAccessors only work with registers of size 1");
        }
        map[RegisterPath(info->registerName)].accessor = _accessor;
      }
      _sharedAccessor = &(map[RegisterPath(info->registerName)]);
      lock = std::move(std::unique_lock<std::mutex>(map[RegisterPath(info->registerName)].mutex, std::defer_lock));
      // allocate and initialise the buffer
      NDRegisterAccessor<UserType>::buffer_2D.resize(1);
      NDRegisterAccessor<UserType>::buffer_2D[0].resize(1);
      NDRegisterAccessor<UserType>::buffer_2D[0][0] = numericToUserType<UserType>(false);
      // set the bit mask
      _bitMask = 1 << info->bit;
    }

    void doReadTransferSynchronously() override {
      assert(lock.owns_lock());
      _accessor->readTransfer();
    }

    bool doWriteTransfer(ChimeraTK::VersionNumber) override {
      assert(lock.owns_lock());
      return _accessor->writeTransfer(_versionNumberTemp);
    }

    void doPreRead(TransferType type) override {
      bool& inProgress = _sharedAccessor->operationInProgressInSameThread();
      if(inProgress) {
        assert(_accessor->isReadTransactionInProgress());
        return;
      }
      inProgress = true;
      lock.lock();
      _accessor->preRead(type);
    }

    void doPostRead(TransferType type, bool hasNewData) override {
      auto unlock = cppext::finally([this] {
        if(this->lock.owns_lock()) {
          assert(_sharedAccessor->operationInProgressInSameThread());
          _sharedAccessor->operationInProgressInSameThread() = false;
          this->lock.unlock();
        }
      });
      _accessor->postRead(type, hasNewData);
      if(!hasNewData) return;
      if(_accessor->accessData(0) & _bitMask) {
        NDRegisterAccessor<UserType>::buffer_2D[0][0] = numericToUserType<UserType>(true);
      }
      else {
        NDRegisterAccessor<UserType>::buffer_2D[0][0] = numericToUserType<UserType>(false);
      }
      this->_versionNumber = {}; // VersionNumber needs to be decoupled from target accessor
      this->_dataValidity = _accessor->dataValidity();
    }

    void doPreWrite(TransferType type, VersionNumber) override {
      bool& inProgress = _sharedAccessor->operationInProgressInSameThread();
      if(inProgress) {
        assert(_accessor->isWriteTransactionInProgress());
      }
      else {
        inProgress = true;
        lock.lock();
      }

      if(!_fixedPointConverter.toRaw<UserType>(NDRegisterAccessor<UserType>::buffer_2D[0][0])) {
        _accessor->accessData(0) &= ~(_bitMask);
      }
      else {
        _accessor->accessData(0) |= _bitMask;
      }

      _versionNumberTemp = {};
      _accessor->setDataValidity(this->_dataValidity);
      _accessor->preWrite(type, _versionNumberTemp);
    }

    void doPostWrite(TransferType type, VersionNumber) override {
      auto unlock = cppext::finally([this] {
        if(this->lock.owns_lock()) {
          assert(_sharedAccessor->operationInProgressInSameThread());
          _sharedAccessor->operationInProgressInSameThread() = false;
          this->lock.unlock();
        }
      });
      _accessor->postWrite(type, _versionNumberTemp);
    }

    bool mayReplaceOther(const boost::shared_ptr<TransferElement const>& other) const override {
      auto rhsCasted = boost::dynamic_pointer_cast<const LNMBackendBitAccessor<UserType>>(other);
      if(!rhsCasted) return false;
      if(_registerPathName != rhsCasted->_registerPathName) return false;
      if(_dev != rhsCasted->_dev) return false;
      return true;
    }

    bool isReadOnly() const override {
      std::lock_guard<std::mutex> guard(*lock.mutex());
      return _accessor->isReadOnly();
    }

    bool isReadable() const override {
      std::lock_guard<std::mutex> guard(*lock.mutex());
      return _accessor->isReadable();
    }

    bool isWriteable() const override {
      std::lock_guard<std::mutex> guard(*lock.mutex());
      return _accessor->isWriteable();
    }

    void setExceptionBackend(boost::shared_ptr<DeviceBackend> exceptionBackend) override {
      std::lock_guard<std::mutex> guard(*lock.mutex());
      this->_exceptionBackend = exceptionBackend;
      _accessor->setExceptionBackend(exceptionBackend);
    }

   protected:
    /// pointer to underlying accessor
    boost::shared_ptr<NDRegisterAccessor<uint64_t>> _accessor;

    /// Lock to be held during a transfer. The mutex lives in the targetAccessorMap of the LogicalNameMappingBackend.
    /// Since we have a shared pointer to that backend, the mutex is always valid.
    std::unique_lock<std::mutex> lock;

    /// Pointer to the SharedAccessor object, needed to call operationInProgressInSameThread().
    LogicalNameMappingBackend::SharedAccessor<uint64_t>* _sharedAccessor;

    /// register and module name
    RegisterPath _registerPathName;

    /// temporary version number passed to the target accessor in write transfers
    /// The VersionNumber needs to be decoupled from target accessor, because the target accessor is used by multiple bit accessors!
    VersionNumber _versionNumberTemp{nullptr};

    /// backend device
    boost::shared_ptr<LogicalNameMappingBackend> _dev;

    /// fixed point converter to handle type conversions from our "raw" type int
    /// to the requested user type. Note: no actual fixed point conversion is
    /// done, it is just used for the type conversion!
    FixedPointConverter _fixedPointConverter;

    /// bit mask for the bit we want to access
    size_t _bitMask;

    std::vector<boost::shared_ptr<TransferElement>> getHardwareAccessingElements() override {
      // cannot lock with a lock_guard, since this might be called inside the TransfereGroup during an ongoing operation
      // which has already locked the mutex.
      bool& inProgress = _sharedAccessor->operationInProgressInSameThread();
      if(!inProgress) {
        lock.lock();
      }
      auto unlock = cppext::finally([&] {
        if(!inProgress) {
          this->lock.unlock();
        }
      });
      return _accessor->getHardwareAccessingElements();
    }

    std::list<boost::shared_ptr<TransferElement>> getInternalElements() override {
      std::lock_guard<std::mutex> guard(*lock.mutex());
      auto result = _accessor->getInternalElements();
      result.push_front(_accessor);
      return result;
    }

    void replaceTransferElement(boost::shared_ptr<TransferElement> newElement) override {
      std::lock_guard<std::mutex> guard(*lock.mutex());
      auto casted = boost::dynamic_pointer_cast<NDRegisterAccessor<uint64_t>>(newElement);
      if(casted && _accessor->mayReplaceOther(newElement)) {
        _accessor = detail::createCopyDecorator<uint64_t>(casted);
      }
      else {
        _accessor->replaceTransferElement(newElement);
      }
      _accessor->setExceptionBackend(this->_exceptionBackend);
    }
  };

  DECLARE_TEMPLATE_FOR_CHIMERATK_USER_TYPES(LNMBackendBitAccessor);

} // namespace ChimeraTK

#endif /* CHIMERA_TK_LNM_BACKEND_BIT_ACCESSOR_H */
