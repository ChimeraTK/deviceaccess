/*
 * CopyRegisterDecorator.h
 *
 *  Created on: Dec 12 2017
 *      Author: Martin Hierholzer
 */

#ifndef CHIMERATK_COPY_REGISTER_DECORATOR_H
#define CHIMERATK_COPY_REGISTER_DECORATOR_H

#include "NDRegisterAccessorDecorator.h"

namespace ChimeraTK {

  /** Runtime type trait to identify CopyRegisterDecorators independent of their type. This is used by the
   *  TransferGroup to find all CopyRegisterDecorators and trigger the postRead() action on them first. */
  struct CopyRegisterDecoratorTrait {};

  /** Decorator for NDRegisterAccessors which makes a copy of the data from the target accessor. This must be used
   *  in implementations of TransferElement::replaceTransferElement() when a used accessor shall be replaced with
   *  an accessor used already in another place and thus a copy of the data shall be made. Note that this decorator
   *  is special in the sense that the TransferGroup will call postRead() on them first. Therefore it is mandatory
   *  to use exactly this implementation (potentially extended by inheritance) and not reimplement it directly based
   *  on the NDRegisterAccessorDecorator<T>. */
  template<typename T>
  struct CopyRegisterDecorator : mtca4u::NDRegisterAccessorDecorator<T>, CopyRegisterDecoratorTrait {

      CopyRegisterDecorator(const boost::shared_ptr<mtca4u::NDRegisterAccessor<T>> &target)
      : mtca4u::NDRegisterAccessorDecorator<T>(target)
      {
        if(!target->isReadable()) {
          throw mtca4u::DeviceException("ChimeraTK::CopyRegisterDecorator: Target accessor is not readable.",
              mtca4u::DeviceException::WRONG_PARAMETER);
        }
      }

      void doPreWrite() override {
        throw mtca4u::DeviceException("ChimeraTK::CopyRegisterDecorator: Accessor is not writeable.",
            mtca4u::DeviceException::WRONG_PARAMETER);
      }

      void doPostRead() override {
        _target->postRead();
        for(size_t i=0; i<_target->getNumberOfChannels(); ++i) buffer_2D[i] = _target->accessChannel(i);
      }

      bool isReadOnly() const override {
        return true;
      }

      bool isWriteable() const override {
        return false;
      }

      using mtca4u::NDRegisterAccessorDecorator<T>::_target;
      using mtca4u::NDRegisterAccessorDecorator<T>::buffer_2D;
  };


}

#endif /* CHIMERATK_COPY_REGISTER_DECORATOR_H */
