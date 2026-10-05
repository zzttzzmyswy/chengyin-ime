#include "myswy_ime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void type_ascii(MyswySession *s, const char *text) {
    for (; *text; ++text) {
        assert(myswy_session_process(s, (uint8_t)*text, 0) == MYSWY_HANDLED);
    }
}
static void expect_text(MyswySession *s, uint32_t field, size_t index, const char *text) {
    uint8_t buffer[MYSWY_MAX_TEXT_BYTES + 1];
    int32_t length = myswy_session_text(s, field, index, NULL, 0);
    if (length != (int32_t)strlen(text) + 1) {
        fprintf(stderr, "text length mismatch: field=%u index=%zu expected=%s required=%d\n",
                field, index, text, length);
    }
    assert(length == (int32_t)strlen(text) + 1);
    memset(buffer, 0x55, sizeof(buffer));
    assert(myswy_session_text(s, field, index, buffer, (size_t)length - 1) == length);
    for (size_t i = 0; i < sizeof(buffer); ++i) { assert(buffer[i] == 0x55); }
    assert(myswy_session_text(s, field, index, buffer, sizeof(buffer)) == length);
    assert(strcmp((char *)buffer, text) == 0);
}

int main(void) {
    {
        const char source[]="zhang\t\xe5\xbc\xa0\t1000\n";
        MyswyDictionary *d=myswy_dictionary_new_tsv((const uint8_t *)source,strlen(source));
        assert(d);
        MyswySession *s=myswy_session_new_with_dictionary(d);
        myswy_dictionary_free(d);
        assert(myswy_session_configure_matching(NULL,0)==MYSWY_INVALID);
        assert(myswy_session_configure_matching(s,1u<<31)==MYSWY_INVALID);
        assert(myswy_session_configure_matching(s,MYSWY_CORRECT_SWAP)==0);
        type_ascii(s,"zhnag");
        assert(myswy_session_configure_matching(s,0)==MYSWY_BUSY);
        expect_text(s,MYSWY_TEXT_PREEDIT,0,"zhnag");
        expect_text(s,MYSWY_TEXT_CANDIDATE_PINYIN,0,"zhang");
        uint8_t marks[8]; memset(marks,0x55,sizeof(marks));
        assert(myswy_session_candidate_marks(NULL,0,NULL,0)==MYSWY_INVALID);
        assert(myswy_session_candidate_marks(s,99,NULL,0)==MYSWY_INVALID);
        assert(myswy_session_candidate_marks(s,0,NULL,0)==5);
        assert(myswy_session_candidate_marks(s,0,marks,4)==5);
        for(size_t i=0;i<sizeof(marks);++i) assert(marks[i]==0x55);
        assert(myswy_session_candidate_marks(s,0,marks,sizeof(marks))==5);
        assert(marks[0]==0 && marks[1]==0 && marks[2]==1 && marks[3]==1 && marks[4]==0 && marks[5]==0x55);
        myswy_session_process(s,MYSWY_KEY_ENTER,0);
        assert(myswy_session_configure_matching(s,0)==0);
        expect_text(s,MYSWY_TEXT_COMMIT,0,"zhnag");
        myswy_session_free(s);
    }
    assert(myswy_ime_abi_version() == MYSWY_ABI_VERSION);
    assert(myswy_session_reset(NULL) == MYSWY_INVALID);
    assert(myswy_session_process(NULL, 'a', 0) == MYSWY_INVALID);
    assert(myswy_session_candidate_count(NULL) == MYSWY_INVALID);
    assert(myswy_session_selected(NULL) == MYSWY_INVALID);
    assert(myswy_session_text(NULL, 0, 0, NULL, 0) == MYSWY_INVALID);
    assert(myswy_session_new_with_dictionary(NULL) == NULL);
    assert(myswy_dictionary_clone(NULL) == NULL);
    assert(myswy_dictionary_new_binary(NULL,0) == NULL);
    assert(myswy_session_page(NULL) == MYSWY_INVALID);
    assert(myswy_session_has_next_page(NULL) == MYSWY_INVALID);
    assert(myswy_session_preedit_cursor(NULL) == MYSWY_INVALID);
    assert(myswy_session_candidate_consumed(NULL,0) == MYSWY_INVALID);
    assert(myswy_session_budget_limited(NULL) == MYSWY_INVALID);
    assert(myswy_session_set_selected(NULL,0) == MYSWY_INVALID);
    assert(myswy_dictionary_new_tsv(NULL, 0) == NULL);
    assert(myswy_session_set_dictionary(NULL, NULL) == MYSWY_INVALID);
    const uint8_t malformed[] = {0xff, 0x00};
    assert(myswy_dictionary_new_tsv(malformed, sizeof(malformed)) == NULL);
    myswy_session_free(NULL);
    myswy_dictionary_free(NULL);

    MyswySession *s = myswy_session_new();
    assert(s);
    assert(myswy_session_selected(s) == -1);
    type_ascii(s, "nihao");
    expect_text(s, MYSWY_TEXT_PREEDIT, 0, "nihao");
    expect_text(s, MYSWY_TEXT_CANDIDATE, 0, "你好");
    assert(myswy_session_text(s, 99, 0, NULL, 0) == MYSWY_INVALID);
    assert(myswy_session_text(s, MYSWY_TEXT_CANDIDATE, 999, NULL, 0) == MYSWY_INVALID);
    assert(myswy_session_preedit_cursor(s)==5 && myswy_session_page(s)==0);
    assert(myswy_session_candidate_consumed(s,0)==5);
    assert(myswy_session_set_selected(s,999)==MYSWY_INVALID);
    assert(myswy_session_set_selected(s,0)==0);
    assert(myswy_session_process(s,MYSWY_KEY_LEFT,0)==MYSWY_HANDLED);
    assert(myswy_session_preedit_cursor(s)==4);
    assert(myswy_session_process(s,MYSWY_KEY_END,0)==MYSWY_HANDLED);
    assert(myswy_session_process(s, 'x', MYSWY_MOD_CONTROL) == 0);
    assert(myswy_session_process(s, 'x', MYSWY_MOD_RELEASE) == 0);
    assert(myswy_session_process(s, 'x', 0x8000) == 0);
    expect_text(s, MYSWY_TEXT_PREEDIT, 0, "nihao");
    assert(myswy_session_process(s, MYSWY_KEY_SPACE, 0) == MYSWY_HANDLED);
    expect_text(s, MYSWY_TEXT_COMMIT, 0, "你好");
    expect_text(s, MYSWY_TEXT_COMMIT, 0, "你好");
    assert(myswy_session_process(s, ',', 0) == 0);
    expect_text(s, MYSWY_TEXT_COMMIT, 0, "");
    type_ascii(s, "xi'an");
    assert(myswy_session_process(s, ',', 0) == 0);
    expect_text(s, MYSWY_TEXT_COMMIT, 0, "西安");
    type_ascii(s, "ni");
    assert(myswy_session_reset(s) == 0);
    expect_text(s, MYSWY_TEXT_PREEDIT, 0, "");
    myswy_session_free(s);

    const char tsv[] = "ce'shi\t自定义词典\t100\n";
    MyswyDictionary *d = myswy_dictionary_new_tsv((const uint8_t *)tsv, strlen(tsv));
    assert(d);
    s = myswy_session_new_with_dictionary(d);
    assert(s);
    MyswyDictionary *demo = myswy_dictionary_new_demo();
    assert(demo);
    assert(myswy_session_set_dictionary(s, NULL) == MYSWY_INVALID);
    type_ascii(s, "ceshi");
    assert(myswy_session_set_dictionary(s, demo) == MYSWY_BUSY);
    expect_text(s, MYSWY_TEXT_PREEDIT, 0, "ceshi");
    expect_text(s, MYSWY_TEXT_CANDIDATE, 0, "自定义词典");
    assert(myswy_session_reset(s) == 0);
    myswy_dictionary_free(d); /* session retains its own reference */
    type_ascii(s, "ceshi");
    assert(myswy_session_process(s, MYSWY_KEY_SELECT_1, 0) == MYSWY_HANDLED);
    expect_text(s, MYSWY_TEXT_COMMIT, 0, "自定义词典");
    assert(myswy_session_set_dictionary(s, demo) == 0);
    expect_text(s, MYSWY_TEXT_COMMIT, 0, "自定义词典");
    myswy_dictionary_free(demo);
    type_ascii(s, "nihao");
    assert(myswy_session_process(s, MYSWY_KEY_SPACE, 0) == MYSWY_HANDLED);
    expect_text(s, MYSWY_TEXT_COMMIT, 0, "你好");
    myswy_session_free(s);
    const char exported[]="'ni'hao 你好 100\n";
    d=myswy_dictionary_new_import((const uint8_t *)exported,strlen(exported));assert(d && myswy_dictionary_entry_count(d)==1);
    int32_t binary_size=myswy_dictionary_binary(d,NULL,0);assert(binary_size>36);
    uint8_t *binary=malloc((size_t)binary_size);assert(binary);memset(binary,0x55,(size_t)binary_size);
    assert(myswy_dictionary_binary(d,binary,(size_t)binary_size-1)==binary_size && binary[0]==0x55);
    assert(myswy_dictionary_binary(d,binary,(size_t)binary_size)==binary_size);
    MyswyDictionary *copy=myswy_dictionary_new_binary(binary,(size_t)binary_size);assert(copy);free(binary);
    MyswyDictionary *merged=myswy_dictionary_merge(d,copy);assert(merged && myswy_dictionary_entry_count(merged)==1);myswy_dictionary_free(d);myswy_dictionary_free(copy);
    s=myswy_session_new_with_dictionary(merged);myswy_dictionary_free(merged);type_ascii(s,"nh");
    assert(myswy_session_process(s,MYSWY_KEY_SPACE,0)==MYSWY_HANDLED);expect_text(s,MYSWY_TEXT_COMMIT,0,"你好");assert(myswy_session_is_association(s)==1 && myswy_session_candidate_consumed(s,0)==0);
    assert(myswy_session_process(s,MYSWY_KEY_TAB,0)==MYSWY_HANDLED);expect_text(s,MYSWY_TEXT_COMMIT,0,"世界");myswy_session_free(s);
    assert(myswy_profile_count(NULL) == MYSWY_INVALID);
    assert(myswy_session_set_profile(NULL, NULL) == MYSWY_INVALID);
    assert(myswy_session_learn_commit(NULL) == MYSWY_INVALID);
    assert(myswy_session_profile(NULL) == NULL);
    assert(myswy_session_configure(NULL, 5, 3) == MYSWY_INVALID);
    MyswyProfile *profile = myswy_profile_new(NULL, 0); assert(profile);
    assert(myswy_profile_record(profile, (const uint8_t *)"nihao", 5, (const uint8_t *)"拟好", 6) == 0);
    assert(myswy_profile_record(profile, (const uint8_t *)"nihao", 5, (const uint8_t *)"ASCII", 5) == MYSWY_INVALID);
    int32_t profile_size = myswy_profile_binary(profile, NULL, 0); assert(profile_size > 20);
    uint8_t *profile_bytes = malloc((size_t)profile_size); assert(profile_bytes);
    memset(profile_bytes, 0x55, (size_t)profile_size);
    assert(myswy_profile_binary(profile, profile_bytes, (size_t)profile_size - 1) == profile_size);
    for (int32_t i = 0; i < profile_size; ++i) assert(profile_bytes[i] == 0x55);
    assert(myswy_profile_binary(profile, profile_bytes, (size_t)profile_size) == profile_size);
    MyswyProfile *restored = myswy_profile_new(profile_bytes, (size_t)profile_size); assert(restored);
    profile_bytes[profile_size - 1] ^= 1;
    assert(myswy_profile_new(profile_bytes, (size_t)profile_size) == NULL);
    free(profile_bytes); myswy_profile_free(profile);
    s = myswy_session_new(); assert(s);
    assert(myswy_session_configure(s, 5, 3) == 0);
    assert(myswy_session_configure(s, 0, 3) == MYSWY_INVALID);
    assert(myswy_session_set_profile(s, restored) == 0);
    myswy_profile_free(restored); /* Session owns an immutable snapshot. */
    type_ascii(s, "nihao");
    expect_text(s, MYSWY_TEXT_CANDIDATE, 0, "拟好");
    expect_text(s, MYSWY_TEXT_DISPLAY_PREEDIT, 0, "ni'hao");
    expect_text(s, MYSWY_TEXT_CANDIDATE_PINYIN, 0, "nihao");
    assert(myswy_session_configure(s, 7, 3) == MYSWY_BUSY);
    assert(myswy_session_process(s, MYSWY_KEY_SPACE, 0) == MYSWY_HANDLED);
    expect_text(s, MYSWY_TEXT_LEARNING_KEY, 0, "nihao");
    assert(myswy_session_learn_commit(s) == 1 && myswy_session_learn_commit(s) == 0);
    MyswyProfile *snapshot = myswy_session_profile(s); assert(snapshot && myswy_profile_count(snapshot) == 1);
    myswy_profile_free(snapshot); myswy_session_free(s);
    myswy_profile_free(NULL);
    puts("C ABI smoke test passed");
    return 0;
}
