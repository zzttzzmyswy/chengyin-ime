// Private regression interface; compiled into myswy_tsf_fixture only.
#pragma once
#include "common.h"
namespace myswy::test {
inline constexpr GUID kConfigurationTest = {0xb3d4ffba, 0x7e1a, 0x4672, {0xb0, 0x39, 0x99, 0x44, 0x18, 0xa4, 0x6c, 0x63}};
struct ConfigurationTest : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Update(UINT pageSize, BOOL punctuation, BOOL associations, BOOL learning,
            const uint8_t *dictionary, size_t size) = 0;
    virtual HRESULT STDMETHODCALLTYPE Appearance(UINT fontSize, UINT layout, BOOL pinyin) = 0;
};
}
