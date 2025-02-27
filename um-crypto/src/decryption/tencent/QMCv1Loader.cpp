#include "um-crypto/decryption/tencent/QMCv1Loader.h"
#include "um-crypto/endian.h"
#include "um-crypto/utils/StringHelper.h"

#include <cassert>

namespace umc::decryption::tencent {

// Private implementation

/**
 * @brief QMCv1 Encryption type.
 */
enum class QMCv1Type {
  /**
   * @brief
   * Used by WeYun, old QQ Music client (with extension e.g. `qmcflac`)
   * Old cipher with static keys.
   */
  kStaticCipher = 0,

  /**
   * @brief
   * Used by QQ Music client (with extension e.g. `mflac`).
   * Same cipher but with a different key for each file.
   * Key derivation parameter is different than {@link kStaticCipher}
   *
   * Do _not_ feed the file footer to this crypto.
   */
  kMapCipher,
};

namespace detail {

constexpr usize kStaticCipherPageSize = 0x7fff;
typedef Arr<u8, kStaticCipherPageSize> QMCv1Cache;

template <QMCv1Type Type>
class QMCv1LoaderImpl : public QMCv1Loader {
 private:
  inline usize GetCacheIndex(const QMCv1Key& key,
                             usize idx_offset,
                             usize i,
                             usize n) const {
    usize index = (i * i + idx_offset) % n;

    if constexpr (Type == QMCv1Type::kMapCipher) {
      u8 v = key[index];
      usize shift = (index + 4) & 0b0111;
      return (v << shift) | (v >> shift);
    }

    return key[index];
  }

  Str name_;
  usize idx_offset_;

 public:
  QMCv1LoaderImpl(const QMCv1Key& key, usize idx_offset)
      : idx_offset_(idx_offset) {
    const char* subtype = Type == QMCv1Type::kStaticCipher ? "static" : "map";
    name_ = utils::Format("QMCv1(%s)", subtype);

    if constexpr (Type == QMCv1Type::kStaticCipher) {
      SetKey(key);
    }
  }

  virtual const Str GetName() const override { return name_; };

  inline void SetKey(const QMCv1Key& key) {
    if (key.empty()) {
      error_ = "key is empty.";
      return;
    }

    error_ = "";
    auto n = key.size();
    usize idx_offset = idx_offset_ % n;

#define QMC_GET_VALUE_AT_IDX(IDX) (GetCacheIndex(key, idx_offset, IDX, n))
    for (usize i = 0; i < kStaticCipherPageSize; i++) {
      cache_[i] = QMC_GET_VALUE_AT_IDX(i);
    }
    value_page_one_ = QMC_GET_VALUE_AT_IDX(kStaticCipherPageSize);
#undef QMC_GET_VALUE_AT_IDX
  }

  inline void SetFooterParser(
      std::shared_ptr<misc::tencent::QMCFooterParser> parser) {
    parser_ = parser;
  }

  virtual usize InitWithFileFooter(const DetectionBuffer& buf) {
    if constexpr (Type == QMCv1Type::kStaticCipher) return 0;

    if (parser_) {
      auto parsed = parser_->Parse(buf.data(), buf.size());
      if (parsed && parsed->key.size() < 300) {
        // Error will be propagated within this method.
        SetKey(parsed->key);
        return parsed->eof_bytes_ignore;
      }
    }

    error_ = "QMC footer parser not set";
    return 0;
  }

 private:
  u8 value_page_one_;
  QMCv1Cache cache_;

  std::shared_ptr<misc::tencent::QMCFooterParser> parser_;

  bool Write(const u8* in, usize len) override {
    if (InErrorState()) return false;

    usize out_size = buf_out_.size();
    buf_out_.resize(out_size + len);
    auto p_out = &buf_out_.at(out_size);

    for (usize i = 0; i < len; i++, offset_++) {
      if (offset_ == kStaticCipherPageSize) {
        p_out[i] = in[i] ^ value_page_one_;
      } else {
        p_out[i] = in[i] ^ cache_[offset_ % kStaticCipherPageSize];
      }
    }

    return true;
  }

  bool End() override { return !InErrorState(); }
};

}  // namespace detail

// Public interface

std::unique_ptr<QMCv1Loader> QMCv1Loader::Create(const QMCv1Key& key) {
  return std::make_unique<detail::QMCv1LoaderImpl<QMCv1Type::kStaticCipher>>(
      key, 80923);
}

std::unique_ptr<QMCv1Loader> QMCv1Loader::Create(
    std::shared_ptr<misc::tencent::QMCFooterParser> parser) {
  auto cipher =
      std::make_unique<detail::QMCv1LoaderImpl<QMCv1Type::kMapCipher>>(
          QMCv1Key{}, 71214);
  cipher->SetFooterParser(parser);
  return cipher;
}

}  // namespace umc::decryption::tencent
