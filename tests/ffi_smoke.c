#include "chengyin_ime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void type_ascii(ChengyinSession *s, const char *text) {
    for (; *text; ++text) {
        assert(chengyin_session_process(s, (uint8_t)*text, 0) == CHENGYIN_HANDLED);
    }
}
static void expect_text(ChengyinSession *s, uint32_t field, size_t index, const char *text) {
    uint8_t buffer[CHENGYIN_MAX_TEXT_BYTES + 1];
    int32_t length = chengyin_session_text(s, field, index, NULL, 0);
    if (length != (int32_t)strlen(text) + 1) {
        fprintf(stderr, "text length mismatch: field=%u index=%zu expected=%s required=%d\n",
                field, index, text, length);
    }
    assert(length == (int32_t)strlen(text) + 1);
    memset(buffer, 0x55, sizeof(buffer));
    assert(chengyin_session_text(s, field, index, buffer, (size_t)length - 1) == length);
    for (size_t i = 0; i < sizeof(buffer); ++i) { assert(buffer[i] == 0x55); }
    assert(chengyin_session_text(s, field, index, buffer, sizeof(buffer)) == length);
    if (strcmp((char *)buffer, text) != 0) {
        fprintf(stderr, "text mismatch: field=%u index=%zu expected=%s actual=%s\n", field, index, text, buffer);
    }
    assert(strcmp((char *)buffer, text) == 0);
}

int main(void) {
    {
        const char source[] = "wo\t我\t1000\nhao\t好\t900\n";
        ChengyinDictionary *d = chengyin_dictionary_new_tsv((const uint8_t *)source, strlen(source));
        ChengyinSession *s = chengyin_session_new_with_dictionary(d);
        chengyin_dictionary_free(d);
        assert(s && chengyin_session_configure_incremental(NULL, 1) == CHENGYIN_INVALID);
        assert(chengyin_session_configure_incremental(s, 2) == CHENGYIN_INVALID);
        assert(chengyin_session_configure_incremental(s, 1) == 0);
        assert(chengyin_session_configure(s, 9, 1) == 0);
        type_ascii(s, "wovvvv");
        assert(chengyin_session_configure_incremental(s, 0) == CHENGYIN_BUSY);
        assert(chengyin_session_candidate_consumed(s, 0) == 2);
        assert(chengyin_session_process(s, CHENGYIN_KEY_SELECT_1, 0) == CHENGYIN_HANDLED);
        expect_text(s, CHENGYIN_TEXT_COMMIT, 0, "我");
        expect_text(s, CHENGYIN_TEXT_PREEDIT, 0, "vvvv");
        expect_text(s, CHENGYIN_TEXT_LEARNING_KEY, 0, "wo");
        assert(chengyin_session_configure_incremental(s, 0) == CHENGYIN_BUSY);
        expect_text(s, CHENGYIN_TEXT_COMMIT, 0, "我");
        assert(chengyin_session_learn_commit(s) == 1);
        assert(chengyin_session_learn_commit(s) == 0);
        chengyin_session_process(s, CHENGYIN_KEY_ESCAPE, 0);
        expect_text(s, CHENGYIN_TEXT_PREEDIT, 0, "");
        expect_text(s, CHENGYIN_TEXT_COMMIT, 0, "");
        assert(chengyin_session_configure_incremental(s, 0) == 0);
        type_ascii(s, "wovvvv");
        chengyin_session_process(s, CHENGYIN_KEY_SELECT_1, 0);
        expect_text(s, CHENGYIN_TEXT_PREEDIT, 0, "我vvvv");
        expect_text(s, CHENGYIN_TEXT_COMMIT, 0, "");
        chengyin_session_free(s);
    }
    {
        const char source[]="zhang\t\xe5\xbc\xa0\t1000\n";
        ChengyinDictionary *d=chengyin_dictionary_new_tsv((const uint8_t *)source,strlen(source));
        assert(d);
        ChengyinSession *s=chengyin_session_new_with_dictionary(d);
        chengyin_dictionary_free(d);
        assert(chengyin_session_configure_matching(NULL,0)==CHENGYIN_INVALID);
        assert(chengyin_session_configure_matching(s,1u<<31)==CHENGYIN_INVALID);
        assert(chengyin_session_configure_matching(s,CHENGYIN_CORRECT_SWAP)==0);
        type_ascii(s,"zhnag");
        assert(chengyin_session_configure_matching(s,0)==CHENGYIN_BUSY);
        expect_text(s,CHENGYIN_TEXT_PREEDIT,0,"zhnag");
        expect_text(s,CHENGYIN_TEXT_CANDIDATE_PINYIN,0,"zhang");
        uint8_t marks[8]; memset(marks,0x55,sizeof(marks));
        assert(chengyin_session_candidate_marks(NULL,0,NULL,0)==CHENGYIN_INVALID);
        assert(chengyin_session_candidate_marks(s,99,NULL,0)==CHENGYIN_INVALID);
        assert(chengyin_session_candidate_marks(s,0,NULL,0)==5);
        assert(chengyin_session_candidate_marks(s,0,marks,4)==5);
        for(size_t i=0;i<sizeof(marks);++i) assert(marks[i]==0x55);
        assert(chengyin_session_candidate_marks(s,0,marks,sizeof(marks))==5);
        assert(marks[0]==0 && marks[1]==0 && marks[2]==1 && marks[3]==1 && marks[4]==0 && marks[5]==0x55);
        chengyin_session_process(s,CHENGYIN_KEY_ENTER,0);
        assert(chengyin_session_configure_matching(s,0)==0);
        expect_text(s,CHENGYIN_TEXT_COMMIT,0,"zhnag");
        chengyin_session_free(s);
    }
    assert(chengyin_ime_abi_version() == CHENGYIN_ABI_VERSION);
    assert(chengyin_session_reset(NULL) == CHENGYIN_INVALID);
    assert(chengyin_session_process(NULL, 'a', 0) == CHENGYIN_INVALID);
    assert(chengyin_session_candidate_count(NULL) == CHENGYIN_INVALID);
    assert(chengyin_session_selected(NULL) == CHENGYIN_INVALID);
    assert(chengyin_session_text(NULL, 0, 0, NULL, 0) == CHENGYIN_INVALID);
    assert(chengyin_session_new_with_dictionary(NULL) == NULL);
    assert(chengyin_dictionary_clone(NULL) == NULL);
    assert(chengyin_dictionary_new_binary(NULL,0) == NULL);
    assert(chengyin_session_page(NULL) == CHENGYIN_INVALID);
    assert(chengyin_session_has_next_page(NULL) == CHENGYIN_INVALID);
    assert(chengyin_session_preedit_cursor(NULL) == CHENGYIN_INVALID);
    assert(chengyin_session_candidate_consumed(NULL,0) == CHENGYIN_INVALID);
    assert(chengyin_session_budget_limited(NULL) == CHENGYIN_INVALID);
    assert(chengyin_session_set_selected(NULL,0) == CHENGYIN_INVALID);
    assert(chengyin_dictionary_new_tsv(NULL, 0) == NULL);
    assert(chengyin_session_set_dictionary(NULL, NULL) == CHENGYIN_INVALID);
    const uint8_t malformed[] = {0xff, 0x00};
    assert(chengyin_dictionary_new_tsv(malformed, sizeof(malformed)) == NULL);
    chengyin_session_free(NULL);
    chengyin_dictionary_free(NULL);

    ChengyinSession *s = chengyin_session_new();
    assert(s);
    assert(chengyin_session_selected(s) == -1);
    type_ascii(s, "nihao");
    expect_text(s, CHENGYIN_TEXT_PREEDIT, 0, "nihao");
    expect_text(s, CHENGYIN_TEXT_CANDIDATE, 0, "你好");
    assert(chengyin_session_text(s, 99, 0, NULL, 0) == CHENGYIN_INVALID);
    assert(chengyin_session_text(s, CHENGYIN_TEXT_CANDIDATE, 999, NULL, 0) == CHENGYIN_INVALID);
    assert(chengyin_session_preedit_cursor(s)==5 && chengyin_session_page(s)==0);
    assert(chengyin_session_candidate_consumed(s,0)==5);
    assert(chengyin_session_set_selected(s,999)==CHENGYIN_INVALID);
    assert(chengyin_session_set_selected(s,0)==0);
    assert(chengyin_session_process(s,CHENGYIN_KEY_LEFT,0)==CHENGYIN_HANDLED);
    assert(chengyin_session_preedit_cursor(s)==4);
    assert(chengyin_session_process(s,CHENGYIN_KEY_END,0)==CHENGYIN_HANDLED);
    assert(chengyin_session_process(s, 'x', CHENGYIN_MOD_CONTROL) == 0);
    assert(chengyin_session_process(s, 'x', CHENGYIN_MOD_RELEASE) == 0);
    assert(chengyin_session_process(s, 'x', 0x8000) == 0);
    expect_text(s, CHENGYIN_TEXT_PREEDIT, 0, "nihao");
    assert(chengyin_session_process(s, CHENGYIN_KEY_SPACE, 0) == CHENGYIN_HANDLED);
    expect_text(s, CHENGYIN_TEXT_COMMIT, 0, "你好");
    expect_text(s, CHENGYIN_TEXT_COMMIT, 0, "你好");
    assert(chengyin_session_process(s, ',', 0) == 0);
    expect_text(s, CHENGYIN_TEXT_COMMIT, 0, "");
    type_ascii(s, "xi'an");
    assert(chengyin_session_process(s, ',', 0) == 0);
    expect_text(s, CHENGYIN_TEXT_COMMIT, 0, "西安");
    type_ascii(s, "ni");
    assert(chengyin_session_reset(s) == 0);
    expect_text(s, CHENGYIN_TEXT_PREEDIT, 0, "");
    chengyin_session_free(s);

    const char tsv[] = "ce'shi\t自定义词典\t100\n";
    ChengyinDictionary *d = chengyin_dictionary_new_tsv((const uint8_t *)tsv, strlen(tsv));
    assert(d);
    s = chengyin_session_new_with_dictionary(d);
    assert(s);
    ChengyinDictionary *demo = chengyin_dictionary_new_demo();
    assert(demo);
    assert(chengyin_session_set_dictionary(s, NULL) == CHENGYIN_INVALID);
    type_ascii(s, "ceshi");
    assert(chengyin_session_set_dictionary(s, demo) == CHENGYIN_BUSY);
    expect_text(s, CHENGYIN_TEXT_PREEDIT, 0, "ceshi");
    expect_text(s, CHENGYIN_TEXT_CANDIDATE, 0, "自定义词典");
    assert(chengyin_session_reset(s) == 0);
    chengyin_dictionary_free(d); /* session retains its own reference */
    type_ascii(s, "ceshi");
    assert(chengyin_session_process(s, CHENGYIN_KEY_SELECT_1, 0) == CHENGYIN_HANDLED);
    expect_text(s, CHENGYIN_TEXT_COMMIT, 0, "自定义词典");
    assert(chengyin_session_set_dictionary(s, demo) == 0);
    expect_text(s, CHENGYIN_TEXT_COMMIT, 0, "自定义词典");
    chengyin_dictionary_free(demo);
    type_ascii(s, "nihao");
    assert(chengyin_session_process(s, CHENGYIN_KEY_SPACE, 0) == CHENGYIN_HANDLED);
    expect_text(s, CHENGYIN_TEXT_COMMIT, 0, "你好");
    chengyin_session_free(s);
    const char exported[]="'ni'hao 你好 100\n";
    d=chengyin_dictionary_new_import((const uint8_t *)exported,strlen(exported));assert(d && chengyin_dictionary_entry_count(d)==1);
    int32_t binary_size=chengyin_dictionary_binary(d,NULL,0);assert(binary_size>36);
    uint8_t *binary=malloc((size_t)binary_size);assert(binary);memset(binary,0x55,(size_t)binary_size);
    assert(chengyin_dictionary_binary(d,binary,(size_t)binary_size-1)==binary_size && binary[0]==0x55);
    assert(chengyin_dictionary_binary(d,binary,(size_t)binary_size)==binary_size);
    ChengyinDictionary *copy=chengyin_dictionary_new_binary(binary,(size_t)binary_size);assert(copy);free(binary);
    ChengyinDictionary *merged=chengyin_dictionary_merge(d,copy);assert(merged && chengyin_dictionary_entry_count(merged)==1);chengyin_dictionary_free(d);chengyin_dictionary_free(copy);
    s=chengyin_session_new_with_dictionary(merged);chengyin_dictionary_free(merged);type_ascii(s,"nh");
    assert(chengyin_session_process(s,CHENGYIN_KEY_SPACE,0)==CHENGYIN_HANDLED);expect_text(s,CHENGYIN_TEXT_COMMIT,0,"你好");assert(chengyin_session_is_association(s)==1 && chengyin_session_candidate_consumed(s,0)==0);
    assert(chengyin_session_process(s,CHENGYIN_KEY_TAB,0)==CHENGYIN_HANDLED);expect_text(s,CHENGYIN_TEXT_COMMIT,0,"世界");chengyin_session_free(s);
    assert(chengyin_profile_count(NULL) == CHENGYIN_INVALID);
    assert(chengyin_session_set_profile(NULL, NULL) == CHENGYIN_INVALID);
    assert(chengyin_session_learn_commit(NULL) == CHENGYIN_INVALID);
    assert(chengyin_session_profile(NULL) == NULL);
    assert(chengyin_session_configure(NULL, 5, 3) == CHENGYIN_INVALID);
    ChengyinProfile *profile = chengyin_profile_new(NULL, 0); assert(profile);
    assert(chengyin_profile_record_selection(NULL, (const uint8_t *)"shi", 3, (const uint8_t *)"是", 3, 0) == CHENGYIN_INVALID);
    assert(chengyin_profile_record_selection(profile, (const uint8_t *)"shi", 3, (const uint8_t *)"是", 3, 1u << 31) == CHENGYIN_INVALID);
    assert(chengyin_profile_count(profile) == 0);
    assert(chengyin_profile_record(profile, (const uint8_t *)"nihao", 5, (const uint8_t *)"拟好", 6) == 0);
    s = chengyin_session_new(); assert(s);
    assert(chengyin_session_set_profile(s, profile) == 0);
    type_ascii(s, "nihao");
    expect_text(s, CHENGYIN_TEXT_CANDIDATE, 0, "你好"); /* Unknown one-off preference is probationary. */
    chengyin_session_free(s);
    for (int i = 0; i < 2; ++i) {
        assert(chengyin_profile_record(profile, (const uint8_t *)"nihao", 5, (const uint8_t *)"拟好", 6) == 0);
    }
    assert(chengyin_profile_record(profile, (const uint8_t *)"nihao", 5, (const uint8_t *)"ASCII", 5) == CHENGYIN_INVALID);
    int32_t profile_size = chengyin_profile_binary(profile, NULL, 0); assert(profile_size > 20);
    uint8_t *profile_bytes = malloc((size_t)profile_size); assert(profile_bytes);
    memset(profile_bytes, 0x55, (size_t)profile_size);
    assert(chengyin_profile_binary(profile, profile_bytes, (size_t)profile_size - 1) == profile_size);
    for (int32_t i = 0; i < profile_size; ++i) assert(profile_bytes[i] == 0x55);
    assert(chengyin_profile_binary(profile, profile_bytes, (size_t)profile_size) == profile_size);
    ChengyinProfile *restored = chengyin_profile_new(profile_bytes, (size_t)profile_size); assert(restored);
    profile_bytes[profile_size - 1] ^= 1;
    assert(chengyin_profile_new(profile_bytes, (size_t)profile_size) == NULL);
    free(profile_bytes); chengyin_profile_free(profile);
    s = chengyin_session_new(); assert(s);
    assert(chengyin_session_configure(s, 5, 3) == 0);
    assert(chengyin_session_configure(s, 0, 3) == CHENGYIN_INVALID);
    assert(chengyin_session_set_profile(s, restored) == 0);
    chengyin_profile_free(restored); /* Session owns an immutable snapshot. */
    type_ascii(s, "nihao");
    expect_text(s, CHENGYIN_TEXT_CANDIDATE, 0, "拟好");
    expect_text(s, CHENGYIN_TEXT_DISPLAY_PREEDIT, 0, "ni'hao");
    expect_text(s, CHENGYIN_TEXT_CANDIDATE_PINYIN, 0, "nihao");
    assert(chengyin_session_configure(s, 7, 3) == CHENGYIN_BUSY);
    assert(chengyin_session_process(s, CHENGYIN_KEY_SPACE, 0) == CHENGYIN_HANDLED);
    expect_text(s, CHENGYIN_TEXT_LEARNING_KEY, 0, "nihao");
    assert(chengyin_session_learn_commit(s) == 1 && chengyin_session_learn_commit(s) == 0);
    ChengyinProfile *snapshot = chengyin_session_profile(s); assert(snapshot && chengyin_profile_count(snapshot) == 1);
    chengyin_profile_free(snapshot); chengyin_session_free(s);
    chengyin_profile_free(NULL);
    puts("C ABI smoke test passed");
    return 0;
}
