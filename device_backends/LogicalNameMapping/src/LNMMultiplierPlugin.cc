#include <boost/make_shared.hpp>

#include "LNMBackendRegisterInfo.h"
#include "LNMAccessorPlugin.h"
#include "NDRegisterAccessorDecorator.h"
#include "TransferElement.h"

namespace ChimeraTK { namespace LNMBackend {

  /********************************************************************************************************************/

  MultiplierPlugin::MultiplierPlugin(
      boost::shared_ptr<LNMBackendRegisterInfo> info, const std::map<std::string, std::string>& parameters)
  : AccessorPlugin(info) {
    // extract parameters
    if(parameters.find("factor") == parameters.end()) {
      throw ChimeraTK::logic_error("LogicalNameMappingBackend MultiplierPlugin: Missing parameter 'factor'.");
    }
    _factor = std::stod(parameters.at("factor"));
  }

  /********************************************************************************************************************/

  void MultiplierPlugin::updateRegisterInfo() {
    // Change data type to non-integral, if we are multiplying with a non-integer factor
    if(std::abs(_factor - std::round(_factor)) <= std::numeric_limits<double>::epsilon()) {
      auto info = _info.lock();
      auto d = info->_dataDescriptor;
      info->_dataDescriptor = ChimeraTK::RegisterInfo::DataDescriptor(d.fundamentalType(), false, false,
          std::numeric_limits<double>::max_digits10, -std::numeric_limits<double>::min_exponent10, d.rawDataType(),
          d.transportLayerDataType());
    }
  }

  /********************************************************************************************************************/

  template<typename UserType>
  struct MultiplierPluginDecorator : ChimeraTK::NDRegisterAccessorDecorator<UserType, double> {
    using ChimeraTK::NDRegisterAccessorDecorator<UserType, double>::buffer_2D;

    MultiplierPluginDecorator(const boost::shared_ptr<ChimeraTK::NDRegisterAccessor<double>>& target, double factor)
    : ChimeraTK::NDRegisterAccessorDecorator<UserType, double>(target), _factor(factor) {}

    void doPreRead(TransferType type) override { _target->preRead(type); }

    void doPostRead(TransferType type, bool hasNewData) override;

    void doPreWrite(TransferType type, VersionNumber) override;

    void doPostWrite(TransferType type, VersionNumber dataLost) override { _target->postWrite(type, dataLost); }

    double _factor;

    using ChimeraTK::NDRegisterAccessorDecorator<UserType, double>::_target;
  };

  template<typename UserType>
  void MultiplierPluginDecorator<UserType>::doPostRead(TransferType type, bool hasNewData) {
    _target->postRead(type, hasNewData);
    if(!hasNewData) return;
    for(size_t i = 0; i < _target->getNumberOfChannels(); ++i) {
      for(size_t k = 0; k < _target->getNumberOfSamples(); ++k) {
        buffer_2D[i][k] = numericToUserType<UserType>(_target->accessData(i, k) * _factor);
      }
    }
    this->_versionNumber = _target->getVersionNumber();
    this->_dataValidity = _target->dataValidity();
  }

  template<typename UserType>
  void MultiplierPluginDecorator<UserType>::doPreWrite(TransferType type, VersionNumber versionNumber) {
    for(size_t i = 0; i < _target->getNumberOfChannels(); ++i) {
      for(size_t k = 0; k < _target->getNumberOfSamples(); ++k) {
        _target->accessData(i, k) = userTypeToNumeric<double>(buffer_2D[i][k]) * _factor;
      }
    }
    _target->setDataValidity(this->_dataValidity);
    _target->preWrite(type, versionNumber);
  }

  /********************************************************************************************************************/

  /** Helper class to implement MultiplierPlugin::decorateAccessor (can later be realised with if constexpr) */
  template<typename UserType, typename TargetType>
  struct MultiplierPlugin_Helper {
    static boost::shared_ptr<NDRegisterAccessor<UserType>> decorateAccessor(
        boost::shared_ptr<NDRegisterAccessor<TargetType>>&, double) {
      assert(false); // only specialisation is valid
      return {};
    }
  };
  template<typename UserType>
  struct MultiplierPlugin_Helper<UserType, double> {
    static boost::shared_ptr<NDRegisterAccessor<UserType>> decorateAccessor(
        boost::shared_ptr<NDRegisterAccessor<double>>& target, double factor) {
      return boost::make_shared<MultiplierPluginDecorator<UserType>>(target, factor);
    }
  };

  template<typename UserType, typename TargetType>
  boost::shared_ptr<NDRegisterAccessor<UserType>> MultiplierPlugin::decorateAccessor(
      boost::shared_ptr<LogicalNameMappingBackend>&, boost::shared_ptr<NDRegisterAccessor<TargetType>>& target) {
    return MultiplierPlugin_Helper<UserType, TargetType>::decorateAccessor(target, _factor);
  }
}} // namespace ChimeraTK::LNMBackend
