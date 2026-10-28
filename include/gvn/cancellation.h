#pragma once
#include <functional>
#include <stdexcept>
namespace gvn {
using CancelCheck=std::function<bool()>;
class OperationCancelled : public std::runtime_error {
public: OperationCancelled():std::runtime_error("Image operation cancelled"){}
};
inline void check_cancelled(const CancelCheck &cancel){if(cancel&&cancel())throw OperationCancelled();}
}
