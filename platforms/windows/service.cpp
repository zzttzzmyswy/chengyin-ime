// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include "candidate.h"
#include "mode_hint.h"
#include "keymap.h"
#include "settings.h"
#include "dictionary_source.h"
#include "preferences.h"
#include "learning_writer.h"
#include "configuration.h"
#include "punctuation.h"
#include "language_bar.h"
#ifdef CHENGYIN_FIXED_TEST_VOCABULARY
#include "test_configuration.h"
#endif
#include <memory>
#include <optional>
#include "ui_element.h"
#include <inputscope.h>
#include <textstor.h>
#include <cwchar>

namespace chengyin {
namespace {
constexpr GUID kInputScope = {0xfde1eaee, 0x6924, 0x4cdf, {0x91, 0xe7, 0xda, 0x38, 0xcf, 0xf5, 0x55, 0x9d}};
constexpr GUID kPropInputScope = {0x1713dd5a, 0x68e7, 0x4a5b, {0x9a, 0xf6, 0x59, 0x2a, 0x59, 0x5c, 0x77, 0x8d}};
// DLL-scoped ownership. A TIP DLL can be unloaded repeatedly, so retain a dictionary
// only while activated services use it. The lock is never taken on the key path.
ChengyinDictionary *embeddedDictionary() {
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(101), RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(module, resource) : nullptr;
    const auto *data = loaded ? static_cast<const uint8_t *>(LockResource(loaded)) : nullptr;
    return data ? chengyin_dictionary_new_binary(data, SizeofResource(module, resource)) : nullptr;
}
// Loads the wanted custom vocabulary, or null when it cannot be read/parsed.
// The caller supplies the built-in base and keeps ownership of it.
ChengyinDictionary *loadCustomDictionary() {
#ifdef CHENGYIN_FIXED_TEST_VOCABULARY
    const std::wstring path;
#else
    const auto path = customDictionaryPath(false);
#endif
    std::vector<uint8_t> bytes;
    if (path.empty() || !readDictionaryFile(path, bytes))
        return nullptr;
    auto *base = embeddedDictionary();
    auto *result = base ? loadEffectiveDictionary(bytes, base) : nullptr;
    chengyin_dictionary_free(base);
    return result;
}
DictionarySource &dictionarySource() {
    // Constructed on first use: the loader reads the custom path, and the
    // fallback builds the embedded vocabulary from this module's resource.
    static DictionarySource source(loadCustomDictionary, embeddedDictionary);
    return source;
}
// The custom vocabulary target this process wants, and its on-disk identity. The
// fixed-vocabulary fixture never reads a real user's file (review R09/R10).
DictionaryStamp customDictionaryStamp() {
#ifdef CHENGYIN_FIXED_TEST_VOCABULARY
    return {};
#else
    return readDictionaryStamp(customDictionaryPath(false));
#endif
}
ChengyinDictionary *acquireDictionary() {
    const auto stamp = customDictionaryStamp();
    return dictionarySource().acquire(stamp.exists, stamp);
}
void releaseDictionary(ChengyinDictionary *owned) {
    dictionarySource().release(owned);
}
// Retries an unfulfilled custom vocabulary whose backoff elapsed. The caller only
// republishes when a snapshot was actually admitted, so a still-locked file cannot
// retire an active session's vocabulary (review R09).
// The fixed-vocabulary fixture has no user file to retry, so the helper exists
// only where a real custom vocabulary can fail (review R09).
#ifndef CHENGYIN_FIXED_TEST_VOCABULARY
bool retryDictionary() {
    const auto stamp = customDictionaryStamp();
    return dictionarySource().retry(stamp.exists, stamp);
}
#endif
class EndEdit final : public ITfEditSession {
  public:
    explicit EndEdit(ITfComposition *composition) : composition_(composition) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfEditSession)
            return E_NOINTERFACE;
        *out = static_cast<ITfEditSession *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (!n)
            delete this;
        return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override {
        // Focus loss preserves the already visible raw preedit. Never edit a new selection.
        return composition_->EndComposition(cookie);
    }
  private:
    ModuleLifetime lifetime_;
    LONG refs_ = 1;
    Ptr<ITfComposition> composition_;
};

bool compartment(IUnknown *object, REFGUID guid) {
    Ptr<ITfCompartmentMgr> manager;
    Ptr<ITfCompartment> value;
    if (FAILED(query(object, IID_ITfCompartmentMgr, manager))
            || FAILED(manager->GetCompartment(guid, value.put())))
        return false;
    VARIANT v{};
    const HRESULT hr = value->GetValue(&v);
    const bool enabled = SUCCEEDED(hr) && v.vt == VT_I4 && v.lVal != 0;
    VariantClear(&v);
    return enabled;
}

bool sensitive(ITfContext *context, TfEditCookie cookie, ITfRange *range) {
    // Legacy Edit/RichEdit password controls may not expose a TSF InputScope.
    HWND focused = GetFocus();
    if (focused && (GetWindowLongPtrW(focused, GWL_STYLE) & ES_PASSWORD)) {
        wchar_t name[64] {};
        GetClassNameW(focused, name, 64);
        if (_wcsicmp(name, L"Edit") == 0 || _wcsnicmp(name, L"RichEdit", 8) == 0)
            return true;
    }
    Ptr<ITfReadOnlyProperty> property;
    if (FAILED(context->GetAppProperty(kPropInputScope, property.put())))
        return false;
    VARIANT value{};
    bool blocked = false;
    if (SUCCEEDED(property->GetValue(cookie, range, &value)) && value.vt == VT_UNKNOWN && value.punkVal) {
        Ptr<ITfInputScope> scope;
        if (SUCCEEDED(query(value.punkVal, kInputScope, scope))) {
            InputScope *scopes = nullptr;
            UINT count = 0;
            if (SUCCEEDED(scope->GetInputScopes(&scopes, &count))) {
                for (UINT i = 0; scopes && i < count; ++i) {
                    if (scopes[i] == IS_PASSWORD || scopes[i] == IS_PRIVATE ||
                            (scopes[i] >= IS_NUMERIC_PASSWORD && scopes[i] <= IS_ALPHANUMERIC_PIN_SET))
                        blocked = true;
                }
            }
            CoTaskMemFree(scopes);
        }
    }
    VariantClear(&value);
    return blocked;
}

HRESULT coreCaret(ChengyinSession *session, TfEditCookie cookie, ITfRange *range, Ptr<ITfRange> &out) {
    uint8_t bytes[CHENGYIN_MAX_TEXT_BYTES + 1] {};
    const int size = chengyin_session_text(session, CHENGYIN_TEXT_PREEDIT, 0, bytes, sizeof(bytes));
    const int cursor = chengyin_session_preedit_cursor(session);
    if (size < 1 || size > static_cast<int>(sizeof(bytes)) || cursor < 0 || cursor >= size)
        return E_FAIL;
    const int length = size == 1 ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                       reinterpret_cast<const char *>(bytes), size - 1, nullptr, 0);
    const int position = cursor == 0 ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                         reinterpret_cast<const char *>(bytes), cursor, nullptr, 0);
    if ((size > 1 && !length) || (cursor > 0 && !position))
        return E_FAIL;
    HRESULT hr = range->Clone(out.put());
    if (SUCCEEDED(hr) && position != length) {
        LONG shifted = 0;
        hr = out->ShiftEnd(cookie, position - length, &shifted, nullptr);
        if (SUCCEEDED(hr) && shifted != position - length)
            hr = E_FAIL;
    }
    if (SUCCEEDED(hr))
        hr = out->Collapse(cookie, TF_ANCHOR_END);
    return hr;
}
bool followInlineCaret(ChengyinSession *session, TfEditCookie cookie, ITfRange *range, ITfRange *selection) {
    LONG before = -1, after = 1, collapsed = 1;
    if (FAILED(selection->CompareStart(cookie, range, TF_ANCHOR_START, &before)) || before < 0
        || FAILED(selection->CompareEnd(cookie, range, TF_ANCHOR_END, &after)) || after > 0
        || FAILED(selection->CompareStart(cookie, selection, TF_ANCHOR_END, &collapsed)) || collapsed) return false;
    Ptr<ITfRange> prefix;
    if (FAILED(range->Clone(prefix.put())) || FAILED(prefix->ShiftEndToRange(cookie, selection, TF_ANCHOR_START))) return false;
    wchar_t text[CHENGYIN_MAX_TEXT_BYTES + 1]{};
    ULONG length = 0;
    if (FAILED(prefix->GetText(cookie, 0, text, CHENGYIN_MAX_TEXT_BYTES, &length))) return false;
    const int position = length ? WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, nullptr, 0, nullptr, nullptr) : 0;
    if (length && position <= 0) return false;
    const int original = chengyin_session_preedit_cursor(session);
    chengyin_session_process(session, CHENGYIN_KEY_HOME, 0);
    const int minimum = chengyin_session_preedit_cursor(session);
    if (position < minimum) {
        for (int i = minimum; i < original; ++i) chengyin_session_process(session, CHENGYIN_KEY_RIGHT, 0);
        return false;
    }
    for (int i = minimum; i < position; ++i) chengyin_session_process(session, CHENGYIN_KEY_RIGHT, 0);
    return chengyin_session_preedit_cursor(session) == position;
}

bool shortcutDown() {
    BYTE state[256] {};
    return GetKeyboardState(state) && ((state[VK_CONTROL] & 0x80) || (state[VK_MENU] & 0x80)
                                       || (state[VK_LWIN] & 0x80) || (state[VK_RWIN] & 0x80));
}
KeyPlan translate(WPARAM key, LPARAM lparam, bool active, bool english, bool association,
                  bool chinesePunctuation) {
    BYTE state[256] {};
    if (!GetKeyboardState(state))
        return {active ? Action::finish : Action::pass, 0, 0};
    const bool shortcut = (state[VK_CONTROL] & 0x80) || (state[VK_MENU] & 0x80) ||
                          (state[VK_LWIN] & 0x80) || (state[VK_RWIN] & 0x80);
    if (key == VK_SPACE && (state[VK_CONTROL] & 0x80) && !(state[VK_MENU] & 0x80) &&
            !(state[VK_LWIN] & 0x80) && !(state[VK_RWIN] & 0x80))
        return {Action::toggle, 0, 0};
    wchar_t translated[8] {};
    char ascii = 0;
    if (!shortcut) {
        const UINT scan = static_cast<UINT>((static_cast<ULONG_PTR>(lparam) >> 16) & 0xff);
        // Windows 10 1607+: flag 4 leaves the keyboard/dead-key state unchanged.
        const int n = ToUnicodeEx(static_cast<UINT>(key), scan, state, translated, 8, 4, GetKeyboardLayout(0));
        if (n == 1 && translated[0] > 0 && translated[0] < 128)
            ascii = static_cast<char>(translated[0]);
        else if (active && key == VK_OEM_7 && !(state[VK_SHIFT] & 0x80))
            ascii = '\'';
    }
    const bool caps = (state[VK_CAPITAL] & 1) != 0;
    const bool shifted = (state[VK_SHIFT] & 0x80) != 0;
    if ((!active || association) && !english && !shortcut && !caps && !shifted && chinesePunctuation
            && PunctuationState::supported(ascii))
        return {Action::punctuation, static_cast<uint32_t>(static_cast<unsigned char>(ascii)), ascii};
    return planKey(static_cast<uint32_t>(key), ascii, shortcut, caps, active, english, association, shifted);
}
}

class Service final : public ProcessorEx, public ITfKeyEventSink,
    public ITfCompositionSink, public ITfThreadMgrEventSink, public ITfTextEditSink, public LayoutSink,
    public ITfCompartmentEventSink
#ifdef CHENGYIN_FIXED_TEST_VOCABULARY
    , public test::ConfigurationTest
#endif
{
  public:
    ~Service() {
        // Stop background loads before releasing dictionaries and profiles.
        watcher_.reset();
        pendingConfiguration_.reset();
        if (session_)
            chengyin_session_free(session_);
        freeDictionary();
        if (profile_)
            chengyin_profile_free(profile_);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_ITfTextInputProcessor || iid == kProcessorEx)
            *out = static_cast<ProcessorEx *>(this);
        else if (iid == IID_ITfKeyEventSink)
            *out = static_cast<ITfKeyEventSink *>(this);
        else if (iid == IID_ITfCompositionSink)
            *out = static_cast<ITfCompositionSink *>(this);
        else if (iid == IID_ITfThreadMgrEventSink)
            *out = static_cast<ITfThreadMgrEventSink *>(this);
        else if (iid == IID_ITfTextEditSink)
            *out = static_cast<ITfTextEditSink *>(this);
        else if (iid == IID_ITfCompartmentEventSink)
            *out = static_cast<ITfCompartmentEventSink *>(this);
#ifdef CHENGYIN_FIXED_TEST_VOCABULARY
        else if (iid == test::kConfigurationTest)
            *out = static_cast<test::ConfigurationTest *>(this);
#endif
        else if (iid == kLayoutSink)
            *out = static_cast<LayoutSink *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (!n)
            delete this;
        return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE Activate(ITfThreadMgr *manager, TfClientId client) override {
        return ActivateEx(manager, client, 0);
    }
    HRESULT STDMETHODCALLTYPE ActivateEx(ITfThreadMgr *manager, TfClientId client, DWORD flags) override {
        if (!manager || client == TF_CLIENTID_NULL)
            return E_INVALIDARG;
        if (manager_)
            return E_UNEXPECTED;
        if (chengyin_ime_abi_version() != CHENGYIN_ABI_VERSION)
            return E_FAIL;
        secure_ = (flags & TF_TMAE_SECUREMODE) != 0;
        uiOnly_ = (flags & TF_TMAE_UIELEMENTENABLEDONLY) != 0;
        query(manager, IID_ITfUIElementMgr, uiManager_);
        if (uiOnly_ && !uiManager_)
            return E_NOINTERFACE;
#ifndef CHENGYIN_FIXED_TEST_VOCABULARY
        DWORD observedConfiguration = 0;
        std::optional<LearningEpoch> configurationEpoch;
#endif
        if (secure_) {
            // Restricted hosts may not grant access to the user's files. Keep
            // this activation entirely in memory, with no learning or settings UI.
            preferences_ = Preferences {};
            preferences_.learning = false;
            preferences_.associations = false;
            profile_ = chengyin_profile_new(nullptr, 0);
        } else {
#ifdef CHENGYIN_FIXED_TEST_VOCABULARY
            preferences_ = Preferences {};
            preferences_.pageSize = 9;
            profile_ = chengyin_profile_new(nullptr, 0);
#else
            configurationEpoch.emplace(kConfigurationEpoch);
            observedConfiguration = configurationEpoch->current();
            preferences_ = loadPreferences(userFile(L"preferences.ini"));
            profile_ = loadProfile(userFile(L"learning.profile"));
            if (!profile_)
                profile_ = chengyin_profile_new(nullptr, 0);
            {
                try {
                    const auto path = userFile(L"learning.profile", true);
                    if (!path.empty())
                        writer_ = std::make_unique<LearningWriter>(path);
                } catch (...) {
                    writer_.reset();
                }
            }
#endif
        }
        english_ = preferences_.defaultEnglish;
        dictionary_ = secure_ ? embeddedDictionary() : acquireDictionary();
        if (!dictionary_) {
            Deactivate();
            return E_OUTOFMEMORY;
        }
        manager_.attach(manager);
        manager->AddRef();
        client_ = client;
        Ptr<ITfKeystrokeMgr> keys;
        HRESULT hr = query(manager, IID_ITfKeystrokeMgr, keys);
        if (SUCCEEDED(hr))
            hr = keys->AdviseKeyEventSink(client, static_cast<ITfKeyEventSink *>(this), TRUE);
        if (SUCCEEDED(hr)) {
            keySink_ = true;
            const TF_PRESERVEDKEY toggle{VK_SPACE, TF_MOD_CONTROL};
            constexpr wchar_t description[] = L"澄音 中英文切换";
            preserved_ = SUCCEEDED(keys->PreserveKey(client_, kToggleKey, &toggle,
                                   description, static_cast<ULONG>(std::wcslen(description))));
        }
        Ptr<ITfSource> source;
        if (SUCCEEDED(hr))
            hr = query(manager, IID_ITfSource, source);
        if (SUCCEEDED(hr))
            hr = source->AdviseSink(IID_ITfThreadMgrEventSink,
                                    static_cast<ITfThreadMgrEventSink *>(this), &threadCookie_);
        if (FAILED(hr))
            Deactivate();
        else {
            subscribeMode();
            if (!secure_ && SUCCEEDED(query(manager_.get(), IID_ITfLangBarItemMgr, languageManager_))) {
                languageItem_.attach(new (std::nothrow) LanguageBarItem([this] {
                    Ptr<Service> keep(this);
                    toggleEnglish();
                }));
                if (!languageItem_ || FAILED(languageManager_->AddItem(languageItem_.get()))) {
                    languageItem_.reset();
                    languageManager_.reset();
                }
            }
            setInputMode();
#ifndef CHENGYIN_FIXED_TEST_VOCABULARY
            if (!secure_) {
                const auto profilePath = userFile(L"learning.profile");
                LearningEpoch learningEpoch(profileEpochName(profilePath).c_str());
                learningGeneration_ = learningEpoch.current();
                LearningEpoch revisionEpoch(profileRevisionName(profilePath).c_str());
                learningRevision_ = revisionEpoch.current();
                try {
                    watcher_ = std::make_unique<ConfigurationWatcher>(observedConfiguration, [] {
                        auto snapshot = std::make_shared<ConfigurationUpdate>();
                        snapshot->preferencesValid = tryLoadPreferences(userFile(L"preferences.ini"), snapshot->preferences);
                        if (!snapshot->preferencesValid && GetFileAttributesW(userFile(L"preferences.ini").c_str())
                                == INVALID_FILE_ATTRIBUTES && (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND))
                            snapshot->preferencesValid = true;
                        snapshot->dictionary = acquireDictionary();
                        snapshot->releaseDictionary = releaseDictionary;
                        snapshot->profile = loadProfile(userFile(L"learning.profile"));
                        LearningEpoch epoch(profileEpochName(userFile(L"learning.profile")).c_str());
                        snapshot->learningGeneration = epoch.current();
                        return snapshot;
                    }, [this](ConfigurationWatcher::Snapshot snapshot) {
                        Ptr<Service>
                        keep(this);
                        receiveConfiguration(std::move(snapshot));
                    }, kConfigurationEpoch, userFile(L"preferences.ini"), profileRevisionName(profilePath),
#ifdef CHENGYIN_FIXED_TEST_VOCABULARY
                       {});
#else
                       [] { return retryDictionary(); });
#endif
                } catch (...) {
                    watcher_.reset();
                }
            }
#endif
        }
        return hr;
    }
#ifdef CHENGYIN_FIXED_TEST_VOCABULARY
    HRESULT STDMETHODCALLTYPE Appearance(UINT fontSize, UINT layout, BOOL pinyin) override {
        if (secure_)
            return E_ACCESSDENIED;
        auto snapshot = std::make_shared<ConfigurationUpdate>();
        snapshot->preferences = preferences_;
        snapshot->preferences.fontSize = static_cast<int>(fontSize);
        snapshot->preferences.layout = static_cast<int>(layout);
        snapshot->preferences.candidatePinyin = pinyin != FALSE;
        snapshot->preferencesValid = validPreferences(snapshot->preferences);
        if (!snapshot->preferencesValid)
            return E_INVALIDARG;
        receiveConfiguration(std::move(snapshot));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Hint(BOOL enabled) override {
        if (secure_)
            return E_ACCESSDENIED;
        auto snapshot = std::make_shared<ConfigurationUpdate>();
        snapshot->preferences = preferences_;
        snapshot->preferences.modeHint = enabled != FALSE;
        snapshot->preferencesValid = true;
        receiveConfiguration(std::move(snapshot));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Update(UINT page, BOOL punctuation, BOOL associations, BOOL learning,
                                     const uint8_t *data, size_t size) override {
        if (secure_)
            return E_ACCESSDENIED;
        if (page != 5 && page != 7 && page != 9)
            return E_INVALIDARG;
        auto snapshot = std::make_shared<ConfigurationUpdate>();
        snapshot->preferences = preferences_;
        snapshot->preferences.pageSize = static_cast<int>(page);
        snapshot->preferences.chinesePunctuation = punctuation != FALSE;
        snapshot->preferences.associations = associations != FALSE;
        snapshot->preferences.learning = learning != FALSE;
        snapshot->preferencesValid = true;
        if (data) {
            snapshot->dictionary = chengyin_dictionary_new_tsv(data, size);
            // The fixture's TSV dictionary is a consumer of the DLL-scoped source
            // bookkeeping too, so it is counted here exactly like an acquired one.
            if (snapshot->dictionary)
                dictionarySource().adopt(snapshot->dictionary);
        } else
            snapshot->dictionary = acquireDictionary();
        snapshot->releaseDictionary = releaseDictionary;
        if (!snapshot->dictionary)
            return E_INVALIDARG;
        receiveConfiguration(std::move(snapshot));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Revision(const uint8_t *key, size_t keySize, const uint8_t *text,
                                       size_t textSize, DWORD revision) override {
        if (secure_)
            return E_ACCESSDENIED;
        // A fixture publishing what another application's ordinary save produces:
        // a profile that learned one selection, offered on the additive channel.
        auto *learned = chengyin_profile_new(nullptr, 0);
        if (!learned || chengyin_profile_record_selection(learned, key, keySize, text, textSize, 0) != 0) {
            if (learned)
                chengyin_profile_free(learned);
            return E_INVALIDARG;
        }
        auto snapshot = std::make_shared<ConfigurationUpdate>();
        snapshot->preferences = preferences_;
        snapshot->preferencesValid = true;
        snapshot->profile = learned;
        snapshot->hasRevision = true;
        snapshot->learningRevision = revision;
        snapshot->learningGeneration = learningGeneration_;
        receiveConfiguration(std::move(snapshot));
        return S_OK;
    }
#endif
    HRESULT STDMETHODCALLTYPE Deactivate() override {
        modeHint_.hide();
        if (languageItem_) {
            languageItem_->detach();
            if (languageManager_)
                languageManager_->RemoveItem(languageItem_.get());
        }
        languageItem_.reset();
        languageManager_.reset();
        watcher_.reset();
        pendingConfiguration_.reset();
        unbind();
        unsubscribeMode();
        writer_.reset();
        if (profile_) {
            chengyin_profile_free(profile_);
            profile_ = nullptr;
        }
        if (manager_) {
            Ptr<ITfSource> source;
            if (threadCookie_ != TF_INVALID_COOKIE && SUCCEEDED(query(manager_.get(), IID_ITfSource, source)))
                source->UnadviseSink(threadCookie_);
            threadCookie_ = TF_INVALID_COOKIE;
            Ptr<ITfKeystrokeMgr> keys;
            if (keySink_ && SUCCEEDED(query(manager_.get(), IID_ITfKeystrokeMgr, keys))) {
                const TF_PRESERVEDKEY toggle{VK_SPACE, TF_MOD_CONTROL};
                if (preserved_)
                    keys->UnpreserveKey(kToggleKey, &toggle);
                keys->UnadviseKeyEventSink(client_);
            }
        }
        keySink_ = false;
        preserved_ = false;
        english_ = false;
        uiManager_.reset();
        uiOnly_ = false;
        manager_.reset();
        client_ = TF_CLIENTID_NULL;
        freeDictionary();
        secure_ = false;
        for (bool &key : eatenKeys_)
            key = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnSetFocus(BOOL foreground) override {
        if (!foreground) {
            // The badge sits at one host's caret, so a focus change retires it.
            modeHint_.hide();
            unbind();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnTestKeyDown(ITfContext *context, WPARAM key, LPARAM lparam,
                                            BOOL *eaten) override {
        if (!eaten)
            return E_POINTER;
        *eaten = FALSE;
        shift_.cancelChord(static_cast<uint32_t>(key));
        if (allowed(context)
                && (ShiftSwitch::matches(static_cast<uint32_t>(key), static_cast<uint64_t>(lparam), preferences_.shiftSwitch)
                    || translate(key, lparam, active(context), english_, association(context),
                                 preferences_.chinesePunctuation).action != Action::pass))
            *eaten = TRUE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnKeyDown(ITfContext *context, WPARAM key, LPARAM lparam, BOOL *eaten) override;
    HRESULT STDMETHODCALLTYPE OnTestKeyUp(ITfContext *, WPARAM key, LPARAM lparam, BOOL *eaten) override {
        if (!eaten)
            return E_POINTER;
        *eaten = (key < 256 && eatenKeys_[key]) || (shift_.pending()
                 && ShiftSwitch::matches(static_cast<uint32_t>(key), static_cast<uint64_t>(lparam), preferences_.shiftSwitch));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnKeyUp(ITfContext *context, WPARAM key, LPARAM lparam, BOOL *eaten) override {
        if (!eaten)
            return E_POINTER;
        *eaten = key < 256 && eatenKeys_[key];
        const bool armed = shift_.pending();
        if (shift_.up(static_cast<uint32_t>(key), static_cast<uint64_t>(lparam), preferences_.shiftSwitch,
                      shortcutDown()) && allowed(context)) {
            toggleEnglish();
            *eaten = TRUE;
        } else if (armed
                   && ShiftSwitch::matches(static_cast<uint32_t>(key), static_cast<uint64_t>(lparam), preferences_.shiftSwitch))
            *eaten = FALSE;
        if (key < 256)
            eatenKeys_[key] = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnChange(REFGUID guid) override {
        if (settingMode_ || !manager_ || (guid != GUID_COMPARTMENT_KEYBOARD_OPENCLOSE
                                          && guid != kConversionMode))
            return S_OK;
        bool next = english_;
        VARIANT value{};
        const auto &comp = guid == GUID_COMPARTMENT_KEYBOARD_OPENCLOSE ? openMode_ : conversionMode_;
        if (comp && SUCCEEDED(comp->GetValue(&value)) && value.vt == VT_I4)
            next = guid == GUID_COMPARTMENT_KEYBOARD_OPENCLOSE ? value.lVal == 0 : !(value.lVal & 1);
        VariantClear(&value);
        if (next != english_) {
            // Captured first: unbind() retires the composition this anchor may live in.
            const CaretAnchor anchor = caretAnchor();
            unbind();
            english_ = next;
            setInputMode();
            // An external switch — the language bar, OPENCLOSE or the conversion
            // mode compartment — is the third trigger path, and it funnels through
            // the same notification as the two keyboard paths.
            notifyModeChanged(anchor);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPreservedKey(ITfContext *context, REFGUID guid, BOOL *eaten) override {
        if (!eaten)
            return E_POINTER;
        *eaten = FALSE;
        if (guid == kToggleKey && allowed(context)) {
            toggleEnglish();
            *eaten = TRUE;
        }
        return S_OK;
    }
    void requestChoice(uint64_t generation, int index);
    HRESULT applyChoice(TfEditCookie cookie, ITfContext *context, uint64_t generation, int index) {
        choicePending_ = false;
        if (generation != uiGeneration_ || !active(context) || !allowed(context) ||
                index < 0 || (index < 9 && index >= chengyin_session_candidate_count(session_)) || index > 11)
            return S_OK;
        editing_ = true;
        BOOL consumed = FALSE;
        const uint32_t key = index == 11 ? static_cast<uint32_t>(CHENGYIN_KEY_ESCAPE) : index == 9 ?
                             static_cast<uint32_t>(CHENGYIN_KEY_PAGE_UP) : index == 10 ? static_cast<uint32_t>
                             (CHENGYIN_KEY_PAGE_DOWN) : CHENGYIN_KEY_SELECT_1 + static_cast<uint32_t>(index);
        const HRESULT hr = edit(cookie, context, {Action::core, key, 0}, consumed);
        editing_ = false;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE OnCompositionTerminated(TfEditCookie, ITfComposition *composition) override {
        if (same(composition_.get(), composition)) {
            composition_.reset();
            associationRange_.reset();
            endElement();
            if (session_)
                chengyin_session_reset(session_);
            ++uiGeneration_;
            candidates_.hide();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnInitDocumentMgr(ITfDocumentMgr *) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnUninitDocumentMgr(ITfDocumentMgr *document) override {
        Ptr<ITfDocumentMgr> current;
        if (context_ && SUCCEEDED(context_->GetDocumentMgr(current.put())) && same(current.get(), document))
            unbind();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnSetFocus(ITfDocumentMgr *document, ITfDocumentMgr *) override {
        Ptr<ITfContext> top;
        if (document)
            document->GetTop(top.put());
        if (!same(context_.get(), top.get()))
            unbind();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPushContext(ITfContext *context) override {
        if (!same(context_.get(), context))
            unbind();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPopContext(ITfContext *context) override {
        if (same(context_.get(), context))
            unbind();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnEndEdit(ITfContext *context, TfEditCookie cookie,
                                        ITfEditRecord *record) override {
        if (editing_ || !same(context_.get(), context) || !active(context))
            return S_OK;
        // Real hosts may deliver our write notification after RequestEditSession
        // returns. Compare the accepted caret/text instead of retiring every edit.
        if (!record || !allowed(context)) {
            unbind();
            return S_OK;
        }
        TF_SELECTION selection{};
        ULONG fetched = 0;
        Ptr<ITfRange> selected, range, expected;
        HRESULT hr = context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
        selected.attach(selection.range);
        if (FAILED(hr) || fetched != 1 || !selected || sensitive(context, cookie, selected.get())) {
            unbind();
            return S_OK;
        }
        if (composition_) {
            hr = composition_->GetRange(range.put());
            if (SUCCEEDED(hr))
                hr = coreCaret(session_, cookie, range.get(), expected);
            wchar_t text[CHENGYIN_MAX_TEXT_BYTES + 1] {};
            ULONG count = 0;
            WideText preedit;
            if (!range || FAILED(range->GetText(cookie, 0, text, CHENGYIN_MAX_TEXT_BYTES, &count))
                    || !readText(session_, CHENGYIN_TEXT_PREEDIT, 0, preedit) || count != static_cast<ULONG>(preedit.length)
                    || std::wmemcmp(text, preedit.data, count)) {
                unbind();
                return S_OK;
            }
        } else
            hr = associationRange_->Clone(expected.put());
        LONG start = 1, end = 1;
        if (FAILED(hr) || !expected || FAILED(selected->CompareStart(cookie, expected.get(), TF_ANCHOR_END, &start))
                || FAILED(selected->CompareEnd(cookie, expected.get(), TF_ANCHOR_END, &end)) || start || end) {
            if (composition_ && range && followInlineCaret(session_, cookie, range.get(), selected.get())) {
                ++uiGeneration_;
                showCandidates(cookie, context);
            } else unbind();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnLayoutChange(ITfContext *context, LayoutCode code, ITfContextView *) override;
    HRESULT refreshLayout(TfEditCookie cookie, ITfContext *context, uint64_t generation) {
        if (!same(context_.get(), context))
            return S_OK;
        uiPending_ = false;
        if (generation != uiGeneration_)
            return S_OK;
        showCandidates(cookie, context);
        return S_OK;
    }
    HRESULT edit(TfEditCookie cookie, ITfContext *context, KeyPlan plan, BOOL &eaten) {
        if (!same(context_.get(), context) || !session_)
            return S_OK;
        TF_SELECTION selection{};
        ULONG fetched = 0;
        const HRESULT selectionResult = context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
        Ptr<ITfRange> selected;
        selected.attach(selection.range);
        if (FAILED(selectionResult) || fetched != 1 || !selected)
            return S_OK;
        if (sensitive(context, cookie, selected.get())) {
            endLocked(cookie);
            return S_OK;
        }
        if (composition_) {
            Ptr<ITfRange> range;
            LONG start = 1, end = 1;
            Ptr<ITfRange> expected;
            if (FAILED(composition_->GetRange(range.put())) || FAILED(coreCaret(session_, cookie, range.get(), expected))
                    ||
                    FAILED(selected->CompareStart(cookie, expected.get(), TF_ANCHOR_END, &start)) ||
                    FAILED(selected->CompareEnd(cookie, expected.get(), TF_ANCHOR_END, &end)) || start || end) {
                endLocked(cookie);
                return S_OK;
            }
        }
        if (associationRange_) {
            LONG start = 1, end = 1;
            if (FAILED(selected->CompareStart(cookie, associationRange_.get(), TF_ANCHOR_END, &start)) ||
                    FAILED(selected->CompareEnd(cookie, associationRange_.get(), TF_ANCHOR_END, &end)) || start || end) {
                endLocked(cookie);
                const bool letter = plan.action == Action::core && ((plan.key >= 'a' && plan.key <= 'z') || (plan.key >= 'A'
                                    && plan.key <= 'Z'));
                if (!letter || !session_ || !same(context_.get(), context))
                    return S_OK;
            }
            if (plan.action == Action::core && (plan.key == CHENGYIN_KEY_ESCAPE || plan.key == CHENGYIN_KEY_UP
                                                || plan.key == CHENGYIN_KEY_DOWN || plan.key == CHENGYIN_KEY_PAGE_UP || plan.key == CHENGYIN_KEY_PAGE_DOWN)) {
                ++uiGeneration_;
                const int result = chengyin_session_process(session_, plan.key, 0);
                eaten = result >= 0 && (result & CHENGYIN_HANDLED);
                if (result < 0 || chengyin_session_is_association(session_) <= 0)
                    endLocked(cookie);
                else
                    showCandidates(cookie, context);
                return S_OK;
            }
        }
        if (plan.action == Action::finish) {
            endLocked(cookie);
            return S_OK;
        }
        if (plan.action == Action::punctuation) {
            wchar_t text[3] {};
            const int length = punctuation_.render(plan.punctuation, !english_ && preferences_.chinesePunctuation, text);
            // Query then mutate once, inside the acquired TSF write lock.
            Ptr<ITfInsertAtSelection> insert;
            Ptr<ITfRange> range;
            if (FAILED(query(context, IID_ITfInsertAtSelection, insert)) ||
                    FAILED(insert->InsertTextAtSelection(cookie, TF_IAS_QUERYONLY, L"", 0, range.put())) ||
                    FAILED(range->SetText(cookie, 0, text, length)))
                return S_OK;
            eaten = TRUE;
            punctuation_.accepted(plan.punctuation, !english_ && preferences_.chinesePunctuation);
            endLocked(cookie);
            if (SUCCEEDED(range->Collapse(cookie, TF_ANCHOR_END))) {
                TF_SELECTION atEnd{range.get(), {TF_AE_NONE, FALSE}};
                context->SetSelection(cookie, 1, &atEnd);
            }
            return S_OK;
        }
        Ptr<ITfRange> range;
        if (!composition_) {
            Ptr<ITfInsertAtSelection> insert;
            Ptr<ITfContextComposition> compositions;
            if (FAILED(query(context, IID_ITfInsertAtSelection, insert)) ||
                    FAILED(query(context, IID_ITfContextComposition, compositions)) ||
                    FAILED(insert->InsertTextAtSelection(cookie, TF_IAS_QUERYONLY, L"", 0, range.put())))
                return S_OK;
            const HRESULT hr = compositions->StartComposition(cookie, range.get(), this, composition_.put());
            if (FAILED(hr) || !composition_)
                return S_OK;
            // The user is composing again; the mode badge would only sit on the caret.
            modeHint_.hide();
        } else if (FAILED(composition_->GetRange(range.put()))) {
            endLocked(cookie);
            return S_OK;
        }
        ++uiGeneration_;
        const int result = chengyin_session_process(session_, plan.key, 0);
        if (result < 0) {
            endLocked(cookie);
            return S_OK;
        }
        WideText preedit, commit;
        if (!readText(session_, CHENGYIN_TEXT_PREEDIT, 0, preedit)
                || !readText(session_, CHENGYIN_TEXT_COMMIT, 0, commit)) {
            endLocked(cookie);
            return S_OK;
        }
        const bool hasCommit = commit.length > 0;
        if (hasCommit && plan.punctuation) {
            wchar_t text[3] {};
            const int n = punctuation_.render(plan.punctuation, !english_ && preferences_.chinesePunctuation, text);
            std::copy_n(text, n, commit.data + commit.length);
            commit.length += n;
        }
        WideText output = hasCommit ? commit : preedit;
        const bool partialCommit = hasCommit && preedit.length > 0;
        if (partialCommit) {
            if (commit.length + preedit.length >= static_cast<int>(std::size(output.data))) {
                endLocked(cookie);
                return S_OK;
            }
            std::copy_n(preedit.data, preedit.length, output.data + output.length);
            output.length += preedit.length;
        }
        if (FAILED(range->SetText(cookie, 0, output.data, output.length))) {
            // The host still has its previous raw text. Abandon state and let this key through.
            endLocked(cookie);
            return S_OK;
        }
        // A successful write must never be replayed, including boundary failures.
        eaten = (result & CHENGYIN_HANDLED) || hasCommit;
        if (partialCommit) {
            Ptr<ITfRange> boundary;
            LONG shifted = 0;
            HRESULT boundaryHr = range->Clone(boundary.put());
            if (SUCCEEDED(boundaryHr)) boundaryHr = boundary->Collapse(cookie, TF_ANCHOR_START);
            if (SUCCEEDED(boundaryHr)) boundaryHr = boundary->ShiftEnd(cookie, commit.length, &shifted, nullptr);
            if (SUCCEEDED(boundaryHr) && shifted != commit.length) boundaryHr = E_FAIL;
            if (SUCCEEDED(boundaryHr)) boundaryHr = boundary->Collapse(cookie, TF_ANCHOR_END);
            if (SUCCEEDED(boundaryHr)) boundaryHr = composition_->ShiftStart(cookie, boundary.get());
            if (SUCCEEDED(boundaryHr)) {
                Ptr<ITfRange> remaining;
                boundaryHr = composition_->GetRange(remaining.put());
                if (SUCCEEDED(boundaryHr)) range.attach(remaining.detach());
            }
            if (FAILED(boundaryHr)) {
                // The replacement can be shorter than the old raw spelling.
                // Leave a valid host caret even when shrinking the composition fails.
                Ptr<ITfRange> fallbackCaret;
                if (SUCCEEDED(range->Clone(fallbackCaret.put()))
                        && SUCCEEDED(fallbackCaret->Collapse(cookie, TF_ANCHOR_END))) {
                    TF_SELECTION fallback{fallbackCaret.get(), {TF_AE_NONE, FALSE}};
                    context->SetSelection(cookie, 1, &fallback);
                }
                endLocked(cookie);
                return S_OK;
            }
        }
        if (hasCommit && plan.punctuation)
            punctuation_.accepted(plan.punctuation, !english_ && preferences_.chinesePunctuation);
        if (hasCommit && !plan.punctuation && session_ && same(context_.get(), context) && preferences_.learning) {
            uint8_t spelling[64] {}, text[CHENGYIN_MAX_TEXT_BYTES + 1] {};
            const int keySize = chengyin_session_text(session_, CHENGYIN_TEXT_LEARNING_KEY, 0, spelling, sizeof(spelling));
            const int textSize = chengyin_session_text(session_, CHENGYIN_TEXT_COMMIT, 0, text, sizeof(text));
            if (chengyin_session_learn_commit(session_) > 0) {
                auto *snapshot = chengyin_session_profile(session_);
                if (snapshot) {
                    if (profile_)
                        chengyin_profile_free(profile_);
                    profile_ = snapshot;
                }
                if (writer_ && keySize > 1 && keySize <= static_cast<int>(sizeof(spelling)) && textSize > 1
                        && textSize <= static_cast<int>(sizeof(text)))
                    writer_->enqueue(spelling, static_cast<size_t>(keySize - 1), text, static_cast<size_t>(textSize - 1), sessionMatchingOptions_);
            }
        }
        // Text was changed: even if a later caret/UI operation fails, do not replay this key.
        eaten = (result & CHENGYIN_HANDLED) || hasCommit;
        Ptr<ITfRange> caret;
        HRESULT hr;
        if (preedit.length == 0) {
            hr = range->Clone(caret.put());
            if (SUCCEEDED(hr))
                hr = caret->Collapse(cookie, TF_ANCHOR_END);
        } else
            hr = coreCaret(session_, cookie, range.get(), caret);
        if (SUCCEEDED(hr)) {
            TF_SELECTION atEnd{caret.get(), {TF_AE_NONE, FALSE}};
            hr = context->SetSelection(cookie, 1, &atEnd);
        }
        if (FAILED(hr)) {
            endLocked(cookie);
            return S_OK;
        }
        if (preedit.length == 0) {
            const bool suggestions = hasCommit && !plan.punctuation && chengyin_session_is_association(session_) > 0;
            const uint64_t retired = uiGeneration_ + 1;
            endLocked(cookie, hasCommit);
            if (suggestions && uiGeneration_ == retired && session_ && same(context_.get(), context)) {
                associationRange_.attach(caret.detach());
                showCandidates(cookie, context);
            }
            return S_OK;
        }
        associationRange_.reset();
        limited_ = (result & CHENGYIN_LIMITED) != 0 || chengyin_session_budget_limited(session_) > 0;
        showCandidates(cookie, context);
        return S_OK;
    }
    void elementAction(uint64_t generation, int index, bool final) {
        if (generation != uiGeneration_ || !session_ || !context_ || !active(context_.get()))
            return;
        if (final) {
            requestChoice(generation, index);
            return;
        }
        if (index == 13) {
            candidates_.hide();
            return;
        }
        if (index == 12) {
            OnLayoutChange(context_.get(), LayoutCode::change, nullptr);
            return;
        }
        if (index >= 0 && index < 9)
            chengyin_session_set_selected(session_, static_cast<size_t>(index));
    }
  private:
    void subscribeMode() {
        Ptr<ITfCompartmentMgr> compartments;
        if (FAILED(query(manager_.get(), IID_ITfCompartmentMgr, compartments)))
            return;
        compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, openMode_.put());
        compartments->GetCompartment(kConversionMode, conversionMode_.put());
        auto advise = [this](ITfCompartment * value, DWORD & cookie) {
            Ptr<ITfSource> source;
            if (value && SUCCEEDED(query(value, IID_ITfSource, source)))
                source->AdviseSink(IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink *>(this), &cookie);
        };
        advise(openMode_.get(), openCookie_);
        advise(conversionMode_.get(), conversionCookie_);
    }
    void unsubscribeMode() {
        auto remove = [](ITfCompartment * value, DWORD cookie) {
            Ptr<ITfSource> source;
            if (value && cookie != TF_INVALID_COOKIE && SUCCEEDED(query(value, IID_ITfSource, source)))
                source->UnadviseSink(cookie);
        };
        remove(openMode_.get(), openCookie_);
        remove(conversionMode_.get(), conversionCookie_);
        openCookie_ = conversionCookie_ = TF_INVALID_COOKIE;
        openMode_.reset();
        conversionMode_.reset();
    }
    void setInputMode() {
        settingMode_ = true;
        VARIANT value{};
        value.vt = VT_I4;
        value.lVal = english_ ? 0 : 1;
        if (openMode_)
            openMode_->SetValue(client_, &value);
        if (conversionMode_)
            conversionMode_->SetValue(client_, &value);
        settingMode_ = false;
        if (languageItem_)
            languageItem_->setEnglish(english_);
    }
    void endElement() {
        Ptr<CandidateElement> old;
        old.attach(element_.detach());
        const DWORD id = std::exchange(elementId_, TF_INVALID_COOKIE);
        if (old)
            old->detach();
        if (id != TF_INVALID_COOKIE && uiManager_)
            uiManager_->EndUIElement(id);
    }
    void showCandidates(TfEditCookie cookie, ITfContext *context) {
        if (!session_ || !active(context)) {
            candidates_.hide();
            return;
        }
        if (uiManager_) {
            if (!element_) {
                auto *element = new (std::nothrow) CandidateElement(this, [](void *target, uint64_t generation, int index,
                bool final) {
                    auto *service = static_cast<Service *>(target);
                    service->AddRef();
                    service->elementAction(generation, index, final);
                    service->Release();
                });
                if (element) {
                    element_.attach(element);
                    Ptr<CandidateElement> retained(element);
                    Ptr<ITfUIElementMgr> manager(uiManager_.get());
                    const uint64_t generation = uiGeneration_;
                    element->update(session_, context, generation);
                    BOOL show = !uiOnly_;
                    DWORD id = TF_INVALID_COOKIE;
                    if (SUCCEEDED(manager->BeginUIElement(element, &show, &id))) {
                        if (element_.get() == element && generation == uiGeneration_ && active(context)) {
                            elementId_ = id;
                            element->Show(show && !uiOnly_);
                        } else
                            manager->EndUIElement(id);
                    } else if (element_.get() == element) {
                        element->detach();
                        element_.reset();
                    }
                }
            } else {
                Ptr<CandidateElement> retained(element_.get());
                Ptr<ITfUIElementMgr> manager(uiManager_.get());
                const DWORD id = elementId_;
                retained->update(session_, context, uiGeneration_);
                if (element_.get() == retained.get())
                    manager->UpdateUIElement(id);
            }
            if (!session_ || !active(context))
                return;
            if (uiOnly_ || (element_ && !element_->shown())) {
                candidates_.hide();
                return;
            }
        }
        Ptr<ITfRange> range, caret;
        Ptr<ITfContextView> view;
        RECT rect{};
        BOOL clipped = FALSE;
        HWND owner = nullptr;
        HRESULT caretResult = E_FAIL;
        if (composition_ && SUCCEEDED(composition_->GetRange(range.put())))
            caretResult = coreCaret(session_, cookie, range.get(), caret);
        else if (associationRange_)
            caretResult = associationRange_->Clone(caret.put());
        bool located = false;
        if (SUCCEEDED(context->GetActiveView(view.put()))) {
            view->GetWnd(&owner);
            if (SUCCEEDED(caretResult))
                located = SUCCEEDED(view->GetTextExt(cookie, caret.get(), &rect, &clipped)) && !clipped
                          && rect.bottom >= rect.top;
            // Some legacy text stores only return extents for nonempty ranges.
            if (!located && range && !clipped)
                located = SUCCEEDED(view->GetTextExt(cookie, range.get(), &rect, &clipped)) && !clipped;
        }
        GUITHREADINFO info{};
        info.cbSize = sizeof(info);
        if (GetGUIThreadInfo(GetCurrentThreadId(), &info)) {
            if (!owner)
                owner = info.hwndFocus ? info.hwndFocus : info.hwndActive;
            if (!located && preferences_.caretFallback && info.hwndCaret && !clipped) {
                POINT first{info.rcCaret.left, info.rcCaret.top}, last{info.rcCaret.right, info.rcCaret.bottom};
                if (ClientToScreen(info.hwndCaret, &first) && ClientToScreen(info.hwndCaret, &last)) {
                    rect = {first.x, first.y, last.x, std::max(last.y, first.y + preferences_.fontSize)};
                    located = true;
                }
            }
        }
        if (!owner) {
            HWND focused = GetFocus();
            owner = focused ? focused : GetForegroundWindow();
        }
        DWORD process = 0;
        if (owner)
            GetWindowThreadProcessId(owner, &process);
        if (!owner || process != GetCurrentProcessId()) {
            candidates_.hide();
            return;
        }
        if (!located && !clipped && haveAnchor_ && IsWindow(anchorOwner_) && anchorOwner_ == owner) {
            rect = anchor_;
            located = true;
        }
        if (!located && !clipped && preferences_.caretFallback && view) {
            RECT screen{};
            if (SUCCEEDED(view->GetScreenExt(&screen)) && screen.right > screen.left && screen.bottom > screen.top) {
                rect = {screen.left + 12, screen.top + 8, screen.left + 13, screen.top + 8 + preferences_.fontSize};
                located = true;
            }
        }
        if (located && session_ && active(context)) {
            anchor_ = rect;
            anchorOwner_ = owner;
            haveAnchor_ = true;
            TF_STATUS status{};
            const bool inlineEditable = composition_ && SUCCEEDED(caretResult) && SUCCEEDED(context->GetStatus(&status))
                                        && !(status.dwStaticFlags & TF_SS_TRANSITORY);
            candidates_.show(session_, owner, rect, limited_, this, [](void *target, uint64_t generation, int index) {
                auto *service = static_cast<Service *>(target);
                service->AddRef();
                service->requestChoice(generation, index);
                service->Release();
            }, uiGeneration_, preferences_, inlineEditable);
            // A visible candidate list is the user composing again; the mode
            // badge would only sit on top of it.
            modeHint_.hide();
        } else
            candidates_.hide();
    }
    // The caret the mode hint anchors to, gathered the way the candidate popup
    // gathers it: the live composition anchor first, then the system caret, then
    // the input view's own corner. The two fallbacks are opt-out, matching
    // caretFallback, so turning that off turns this off too.
    struct CaretAnchor {
        bool located = false;
        RECT rect{};
        HWND owner = nullptr;
    };
    CaretAnchor caretAnchor() const {
        CaretAnchor anchor;
        if (haveAnchor_ && IsWindow(anchorOwner_)) {
            anchor = {true, anchor_, anchorOwner_};
            return anchor;
        }
        GUITHREADINFO info{};
        info.cbSize = sizeof(info);
        if (GetGUIThreadInfo(GetCurrentThreadId(), &info)) {
            anchor.owner = info.hwndFocus ? info.hwndFocus : info.hwndActive;
            if (preferences_.caretFallback && info.hwndCaret) {
                POINT first{info.rcCaret.left, info.rcCaret.top}, last{info.rcCaret.right, info.rcCaret.bottom};
                if (ClientToScreen(info.hwndCaret, &first) && ClientToScreen(info.hwndCaret, &last)) {
                    anchor.rect = {first.x, first.y, last.x,
                                   std::max<LONG>(last.y, first.y + preferences_.fontSize)};
                    anchor.located = true;
                    return anchor;
                }
            }
        }
        if (!anchor.owner) {
            HWND focused = GetFocus();
            anchor.owner = focused ? focused : GetForegroundWindow();
        }
        // Without a caret the hint still belongs somewhere predictable: the top
        // of the owning window, where a status badge is least in the way.
        if (preferences_.caretFallback && anchor.owner) {
            RECT client{};
            if (GetClientRect(anchor.owner, &client)) {
                POINT origin{client.left, client.top};
                if (ClientToScreen(anchor.owner, &origin)) {
                    anchor.rect = {origin.x + 12, origin.y + 8, origin.x + 13,
                                   origin.y + 8 + preferences_.fontSize};
                    anchor.located = true;
                    return anchor;
                }
            }
        }
        return anchor;
    }
    // The single notification point every mode change funnels through, so no
    // trigger path can pop the hint twice for one switch. The caller captures the
    // anchor before it retires the composition the anchor came from.
    void notifyModeChanged(const CaretAnchor &anchor) {
        if (!preferences_.modeHint || !anchor.located || !anchor.owner) {
            modeHint_.hide();
            return;
        }
        // The badge is drawn by this process; a foreign host's caret coordinates
        // are not ours to place a window over.
        DWORD process = 0;
        GetWindowThreadProcessId(anchor.owner, &process);
        if (process != GetCurrentProcessId()) {
            modeHint_.hide();
            return;
        }
        modeHint_.show(anchor.owner, anchor.rect, english_, preferences_, GetTickCount64());
    }
    void toggleEnglish() {
        // Captured first: unbind() retires the composition this anchor may live in.
        const CaretAnchor anchor = caretAnchor();
        unbind();
        english_ = !english_;
        setInputMode();
        notifyModeChanged(anchor);
    }
    bool active(ITfContext *context) const {
        return (composition_ || associationRange_) && same(context_.get(), context);
    }
    bool association(ITfContext *context) const {
        return associationRange_ && session_ && same(context_.get(), context)
               && chengyin_session_is_association(session_) > 0;
    }
    bool allowed(ITfContext *context) const {
        if (!manager_ || !context || editing_)
            return false;
        TF_STATUS status{};
        if (FAILED(context->GetStatus(&status)) || (status.dwDynamicFlags & TS_SD_READONLY))
            return false;
        return !compartment(context, GUID_COMPARTMENT_KEYBOARD_DISABLED)
               && !compartment(context, GUID_COMPARTMENT_EMPTYCONTEXT);
    }
    void endLocked(TfEditCookie cookie, bool preserve = false) {
        ++uiGeneration_;
        Ptr<ITfComposition> old;
        old.attach(composition_.detach());
        associationRange_.reset();
        if (session_ && !preserve)
            chengyin_session_reset(session_);
        endElement();
        candidates_.hide();
        if (old)
            old->EndComposition(cookie);
    }
    void unbind() {
        punctuation_.reset();
        associationRange_.reset();
        shift_.reset();
        haveAnchor_ = false;
        anchorOwner_ = nullptr;
        ++uiGeneration_;
        uiPending_ = false;
        for (bool &key : eatenKeys_)
            key = false;
        Ptr<ITfContext> context;
        context.attach(context_.detach());
        if (context && editCookie_ != TF_INVALID_COOKIE) {
            Ptr<ITfSource> source;
            if (SUCCEEDED(query(context.get(), IID_ITfSource, source)))
                source->UnadviseSink(editCookie_);
        }
        editCookie_ = TF_INVALID_COOKIE;
        if (context && layoutCookie_ != TF_INVALID_COOKIE) {
            Ptr<ITfSource> source;
            if (SUCCEEDED(query(context.get(), IID_ITfSource, source)))
                source->UnadviseSink(layoutCookie_);
        }
        layoutCookie_ = TF_INVALID_COOKIE;
        Ptr<ITfComposition> old;
        old.attach(composition_.detach());
        endElement();
        if (session_) {
            chengyin_session_free(session_);
            session_ = nullptr;
        }
        candidates_.hide();
        if (old && context && client_ != TF_CLIENTID_NULL) {
            auto *end = new (std::nothrow) EndEdit(old.get());
            if (end) {
                HRESULT result = E_FAIL;
                context->RequestEditSession(client_, end, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &result);
                end->Release();
            }
        }
    }
    bool bind(ITfContext *context) {
        applyPendingConfiguration();
        if (same(context_.get(), context))
            return session_ != nullptr;
        unbind();
        session_ = chengyin_session_new_with_dictionary(dictionary_);
        if (!session_)
            return false;
        chengyin_session_configure(session_, static_cast<uint32_t>(preferences_.pageSize),
                                (preferences_.learning ? 1u : 0u) | (preferences_.associations ? 2u : 0u));
        chengyin_session_configure_incremental(session_, 1);
        sessionMatchingOptions_ = chengyin_session_configure_matching(session_,preferences_.matchingOptions) == 0
                                  ? preferences_.matchingOptions : 0;
        if (profile_)
            chengyin_session_set_profile(session_, profile_);
        context_.attach(context);
        context->AddRef();
        Ptr<ITfSource> source;
        if (FAILED(query(context, IID_ITfSource, source)) || FAILED(source->AdviseSink(
                    IID_ITfTextEditSink, static_cast<ITfTextEditSink *>(this), &editCookie_))) {
            unbind();
            return false;
        }
        // Older TSF hosts may not expose layout notifications; input still works.
        if (FAILED(source->AdviseSink(kLayoutSink, static_cast<LayoutSink *>(this), &layoutCookie_)))
            layoutCookie_ = TF_INVALID_COOKIE;
        return true;
    }
    void receiveConfiguration(ConfigurationWatcher::Snapshot snapshot) {
        if (secure_)
            return;
        const bool wasLearning = preferences_.learning;
        if (snapshot->preferencesValid) {
            if (preferences_.shiftSwitch != snapshot->preferences.shiftSwitch)
                shift_.reset();
            preferences_ = snapshot->preferences;
        }
        // Only a moved generation (clear/import) or learning being switched back on
        // may discard this service's snapshot. An ordinary learning revision is
        // additive and must not invalidate anything this service has queued.
        replaceProfile_ = replaceProfile_ || !wasLearning || snapshot->learningGeneration != learningGeneration_;
        revisionOnly_ = !replaceProfile_ && snapshot->hasRevision
                        && snapshot->learningRevision != learningRevision_;
        pendingConfiguration_ = std::move(snapshot);
        applyPendingConfiguration();
        if (session_ && composition_)
            candidates_.refreshPreferences(session_, preferences_);
        if (composition_ && context_)
            OnLayoutChange(context_.get(), LayoutCode::change, nullptr);
    }
    void applyPendingConfiguration() {
        if (!pendingConfiguration_ || composition_)
            return;
        // A snapshot carrying only a newer ordinary revision is a pure reload of
        // another application's selections. Adopting it must not retire the active
        // session, so it never unbinds and never touches the dictionary.
        if (revisionOnly_) {
            // The on-disk snapshot is older than a selection this service already
            // made: either one is still queued, or this service itself published
            // at least this revision. Adopting it would roll the in-memory profile
            // back behind a confirmed choice, so it waits for the next revision.
            if (writer_ && writer_->supersedes(pendingConfiguration_->learningRevision))
                return;
            auto next = std::move(pendingConfiguration_);
            if (next->profile) {
                if (profile_)
                    chengyin_profile_free(profile_);
                profile_ = std::exchange(next->profile, nullptr);
                // An idle session exists only for post-commit associations; pushing
                // the merged profile in makes the other application's selection take
                // effect without discarding that association.
                if (session_)
                    chengyin_session_set_profile(session_, profile_);
            }
            learningRevision_ = next->learningRevision;
            revisionOnly_ = false;
            return;
        }
        auto next = std::move(pendingConfiguration_);
        const bool destructive = replaceProfile_;
        unbind(); // Only idle/association state is retired; raw input is preserved above.
        if (next->dictionary) {
            if (dictionary_)
                releaseDictionary(dictionary_);
            dictionary_ = std::exchange(next->dictionary, nullptr);
        }
        if (destructive && next->profile) {
            if (profile_)
                chengyin_profile_free(profile_);
            profile_ = std::exchange(next->profile, nullptr);
            learningGeneration_ = next->learningGeneration;
        }
        if (next->hasRevision)
            learningRevision_ = next->learningRevision;
        replaceProfile_ = false;
        revisionOnly_ = false;
    }
    void freeDictionary() {
        if (!dictionary_)
            return;
        if (secure_)
            chengyin_dictionary_free(dictionary_);
        else
            releaseDictionary(dictionary_);
        dictionary_ = nullptr;
    }
    ModuleLifetime lifetime_;
    LONG refs_ = 1;
    Ptr<ITfThreadMgr> manager_;
    Ptr<ITfUIElementMgr> uiManager_;
    Ptr<CandidateElement> element_;
    DWORD elementId_ = TF_INVALID_COOKIE;
    bool uiOnly_ = false;
    bool secure_ = false;
    TfClientId client_ = TF_CLIENTID_NULL;
    DWORD threadCookie_ = TF_INVALID_COOKIE;
    DWORD editCookie_ = TF_INVALID_COOKIE;
    DWORD layoutCookie_ = TF_INVALID_COOKIE;
    bool keySink_ = false;
    bool preserved_ = false;
    bool english_ = false;
    bool choicePending_ = false;
    bool editing_ = false;
    bool uiPending_ = false;
    bool limited_ = false;
    uint64_t uiGeneration_ = 0;
    bool eatenKeys_[256] {};
    Ptr<ITfContext> context_;
    Ptr<ITfComposition> composition_;
    Ptr<ITfRange> associationRange_;
    ChengyinSession *session_ = nullptr;
    uint32_t sessionMatchingOptions_ = 0; // frozen with the composition, unlike a new preference snapshot
    ChengyinDictionary *dictionary_ = nullptr;
    CandidateWindow candidates_;
    ModeHintWindow modeHint_;
    Preferences preferences_{};
    Ptr<ITfLangBarItemMgr> languageManager_;
    Ptr<LanguageBarItem> languageItem_;
    PunctuationState punctuation_;
    std::unique_ptr<ConfigurationWatcher> watcher_;
    ConfigurationWatcher::Snapshot pendingConfiguration_;
    DWORD learningGeneration_ = 0;
    DWORD learningRevision_ = 0;
    bool replaceProfile_ = false;
    // Set when the pending snapshot carries only a newer ordinary revision, so it
    // is adopted without retiring the active session.
    bool revisionOnly_ = false;
    ShiftSwitch shift_;
    ChengyinProfile *profile_ = nullptr;
    std::unique_ptr<LearningWriter> writer_;
    Ptr<ITfCompartment> openMode_, conversionMode_;
    DWORD openCookie_ = TF_INVALID_COOKIE, conversionCookie_ = TF_INVALID_COOKIE;
    bool settingMode_ = false;
    RECT anchor_{};
    HWND anchorOwner_ = nullptr;
    bool haveAnchor_ = false;
};

namespace {
class LayoutEdit final : public ITfEditSession {
  public:
    LayoutEdit(Service *service, ITfContext *context, uint64_t generation)
        : service_(service), context_(context), generation_(generation) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfEditSession)
            return E_NOINTERFACE;
        *out = static_cast<ITfEditSession *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (!n)
            delete this;
        return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override {
        return service_->refreshLayout(cookie, context_.get(), generation_);
    }
  private:
    ModuleLifetime lifetime_;
    LONG refs_ = 1;
    Ptr<Service> service_;
    Ptr<ITfContext> context_;
    uint64_t generation_;
};
class ChoiceEdit final : public ITfEditSession {
  public:
    ChoiceEdit(Service *service, ITfContext *context, uint64_t generation, int index)
        : service_(service), context_(context), generation_(generation), index_(index) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfEditSession)
            return E_NOINTERFACE;
        *out = static_cast<ITfEditSession *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (!n)
            delete this;
        return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override {
        return service_->applyChoice(cookie, context_.get(), generation_, index_);
    }
  private:
    ModuleLifetime lifetime_;
    LONG refs_ = 1;
    Ptr<Service> service_;
    Ptr<ITfContext> context_;
    uint64_t generation_;
    int index_;
};
class KeyEdit final : public ITfEditSession {
  public:
    KeyEdit(Service *service, ITfContext *context, KeyPlan plan) : service_(service), context_(context),
        plan_(plan) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_ITfEditSession)
            return E_NOINTERFACE;
        *out = static_cast<ITfEditSession *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&refs_));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (!n)
            delete this;
        return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE DoEditSession(TfEditCookie cookie) override {
        return service_->edit(cookie, context_.get(), plan_, eaten);
    }
    BOOL eaten = FALSE;
  private:
    ModuleLifetime lifetime_;
    LONG refs_ = 1;
    Ptr<Service> service_;
    Ptr<ITfContext> context_;
    KeyPlan plan_;
};
}
void Service::requestChoice(uint64_t generation, int index) {
    if (choicePending_ || generation != uiGeneration_ || !context_ || !active(context_.get()))
        return;
    auto *choice = new (std::nothrow) ChoiceEdit(this, context_.get(), generation, index);
    if (!choice)
        return;
    choicePending_ = true;
    HRESULT result = E_FAIL;
    const HRESULT hr = context_->RequestEditSession(client_, choice, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE,
                       &result);
    if (FAILED(hr) || FAILED(result))
        choicePending_ = false;
    choice->Release();
}
HRESULT Service::OnLayoutChange(ITfContext *context, LayoutCode code, ITfContextView *) {
    if (!same(context_.get(), context))
        return S_OK;
    if (code == LayoutCode::destroy) {
        ++uiGeneration_;
        haveAnchor_ = false;
        anchorOwner_ = nullptr;
        candidates_.hide();
        return S_OK;
    }
    if (!active(context) || uiPending_ || client_ == TF_CLIENTID_NULL)
        return S_OK;
    auto *update = new (std::nothrow) LayoutEdit(this, context, uiGeneration_);
    if (!update)
        return E_OUTOFMEMORY;
    const uint64_t generation = uiGeneration_;
    uiPending_ = true;
    HRESULT result = E_FAIL;
    const HRESULT hr = context->RequestEditSession(client_, update, TF_ES_ASYNCDONTCARE | TF_ES_READ, &result);
    if ((FAILED(hr) || FAILED(result)) && generation == uiGeneration_)
        uiPending_ = false;
    update->Release();
    return S_OK;
}
HRESULT Service::OnKeyDown(ITfContext *context, WPARAM key, LPARAM lparam, BOOL *eaten) {
    if (!eaten)
        return E_POINTER;
    *eaten = FALSE;
    if (!allowed(context))
        return S_OK;
    applyPendingConfiguration();
    shift_.down(static_cast<uint32_t>(key), static_cast<uint64_t>(lparam), preferences_.shiftSwitch,
                shortcutDown());
    if (ShiftSwitch::matches(static_cast<uint32_t>(key), static_cast<uint64_t>(lparam),
                             preferences_.shiftSwitch))
        return S_OK;
    const KeyPlan plan = translate(key, lparam, active(context), english_, association(context),
                                   preferences_.chinesePunctuation);
    if (plan.action == Action::toggle) {
        if (!(static_cast<ULONG_PTR>(lparam) & (1ULL << 30)))
            toggleEnglish();
        *eaten = TRUE;
        if (key < 256 && manager_)
            eatenKeys_[key] = true;
        return S_OK;
    }
    if (plan.action == Action::pass || !bind(context))
        return S_OK;
    auto *editSession = new (std::nothrow) KeyEdit(this, context, plan);
    if (!editSession)
        return E_OUTOFMEMORY;
    editing_ = true;
    HRESULT result = E_FAIL;
    // Process the core only inside an acquired synchronous write lock. If TSF
    // denies it, no input has been consumed and there is no queued key edit.
    context->RequestEditSession(client_, editSession, TF_ES_SYNC | TF_ES_READWRITE, &result);
    editing_ = false;
    *eaten = editSession->eaten;
    if (key < 256 && manager_ && same(context_.get(), context))
        eatenKeys_[key] = *eaten != FALSE;
    editSession->Release();
    return S_OK;
}
HRESULT createService(REFIID iid, void **out) {
    auto *service = new (std::nothrow) Service;
    if (!service)
        return E_OUTOFMEMORY;
    const HRESULT hr = service->QueryInterface(iid, out);
    service->Release();
    return hr;
}
}
