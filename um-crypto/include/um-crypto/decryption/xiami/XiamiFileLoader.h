#pragma once

#include "../DecryptionStream.h"

namespace umc::decryption::xiami {

class XiamiFileLoader : public DecryptionStream {
 public:
  virtual const Str GetName() const override { return "Xiami"; };
  static std::unique_ptr<XiamiFileLoader> Create();
};

}  // namespace umc::decryption::xiami
