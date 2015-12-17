/*
 * Copyright (c) 2015      Intel, Inc.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */
#include "orcm/util/logical_group.h"
#include "orcm/constants.h"
#include "orte/util/regex.h"
#include "opal/mca/installdirs/installdirs.h"

/* Initialization the logical grouping */
static int orcm_logical_group_init(char *config_file);

/* whether the member to be added is already exist */
static bool member_exist(opal_list_t *group_members, char *new_member);

/* internal function to add members to a group */
static int orcm_logical_group_add_internal(char *tag, char **new_members,
                                           opal_list_t *group_members,
                                           opal_hash_table_t *io_groups);

/* check if all the members do exist in a group */
static int orcm_logical_group_all_members_exist_one_tag(opal_list_t *value, char **members);

/* check if all the members do exist in all groups */
static int orcm_logical_group_all_members_exist_all_tags(opal_hash_table_t *groups,
                                                         char **members);

/* remove members of a group in the hash table */
static int orcm_logical_group_hash_table_remove_members(char *tag,
                                                        opal_list_t *value,
                                                        char **members,
                                                        opal_hash_table_t *io_groups);

/* remove members of all groups */
static int orcm_logical_group_remove_all_tags(char **members, int do_all_member,
                                              opal_hash_table_t *io_groups);

/* remove members of a group */
static int orcm_logical_group_remove_a_tag(char *tag, char **members, int do_all_member,
                                           opal_hash_table_t *io_groups);

/* internal function to remove members from groups */
static int orcm_logical_group_remove_internal(char *tag, int do_all_tag,
                                              char **members, int do_all_member,
                                              opal_hash_table_t *io_groups);

/* find the specified members in the list of members */
static opal_list_t *orcm_logical_group_list_specific_members(opal_list_t *value,
                                                             char **members);

/* append a list of members to the member list that is an opal list */
static int orcm_logical_group_list_append(char *memberlist, opal_list_t *members_list);

/* list the members of all tags */
static opal_hash_table_t* orcm_logical_group_list_all_tags(char **members, int do_all_member,
                                                           opal_hash_table_t *groups);

/* list the members of a tag */
static opal_hash_table_t *orcm_logical_group_list_a_tag(char *tag, char **members,
                                                        int do_all_member,
                                                        opal_hash_table_t *groups);

/* internal function to list members of groups */
static opal_hash_table_t *orcm_logical_group_list_internal(char *tag, int do_all_tag,
                                                           char **members,
                                                           int do_all_member,
                                                           opal_hash_table_t *groups);
/* open the storage file with a specific mode */
static FILE *orcm_logical_group_open_file(char *storage_filename,
                                          char *mode, bool *o_file_missing);

static int orcm_logical_group_close_file(void);

/* load the content of the storage file to an in-memory hash table */
static int orcm_logical_group_load_from_file(char *storage_filename,
                                             opal_hash_table_t *io_groups);

/* whether a line is a comment or not */
static int orcm_logical_group_is_comment(char *line);

/* split a line in a file */
static int orcm_logical_group_split_line(char *line, char ***o_line_fields);

/* process a line that is a tag */
static int orcm_logical_group_process_tag_line(char *tag);

/* process a line */
static int orcm_logical_group_process_line(char *line, opal_hash_table_t *io_groups);

/* trim a line */
static void orcm_logical_group_trim_string(char *string, char **o_string);

/* get a new line from the storage file */
static int orcm_logical_group_get_newline(FILE *storage_file, char *io_line,
                                          int max_line_length, int *o_eof);

/* parsing a storage file */
static int orcm_logical_group_parsing(char *line_buf, opal_hash_table_t *io_groups);

/* pass the storage file */
static int orcm_logical_group_parse_from_file(opal_hash_table_t *io_groups);

/* combining multiple list item of members into one */
static opal_list_t *orcm_logical_group_do_convertion(opal_list_t *members_list,
                                                     char *memberlist,
                                                     unsigned int reserved_size);

/* concatenate multiple list item of members into one */
static int orcm_logical_group_save_to_file_concat(char *tag, opal_list_t *members_list);

/* internal function to save the in-memory content to a storage file */
static int orcm_logical_group_save_to_file_internal(opal_hash_table_t *groups);

/* dereference a group name to an array of members */
static int orcm_logical_group_tag_to_members_nested(char ***tag_array, char ***o_array_string);

/* calculate the memory size of all the members in an array of string */
static unsigned int orcm_logical_group_argv_addup_size(char **argv, int *count);

void logical_group_member_construct(orcm_logical_group_member_t *member_item)
{
    member_item->member = NULL;
}

void logical_group_member_destruct(orcm_logical_group_member_t *member_item)
{
    SAFEFREE(member_item->member);
}

OBJ_CLASS_INSTANCE(orcm_logical_group_member_t, opal_list_item_t,
                   logical_group_member_construct, logical_group_member_destruct);

orcm_logical_group_t LOGICAL_GROUP = {NULL, NULL};
char *current_tag = NULL;

file_with_lock_t logical_group_file_lock = {NULL, -1, {F_RDLCK, SEEK_SET, 0, 0, 0}};

static int orcm_logical_group_init(char *config_file)
{
    int erri = ORCM_SUCCESS;

    if (NULL == config_file) {
        if (-1 == (erri = asprintf(&(LOGICAL_GROUP.storage_filename),
                   "%s/etc/orcm-logical-grouping.txt", opal_install_dirs.prefix))) {
            return ORCM_ERR_OUT_OF_RESOURCE;
        }
    } else if (NULL == (LOGICAL_GROUP.storage_filename = strdup(config_file))) {
        return ORCM_ERR_OUT_OF_RESOURCE;
    }

    if (NULL == (LOGICAL_GROUP.groups = OBJ_NEW(opal_hash_table_t))) {
            return ORCM_ERR_OUT_OF_RESOURCE;
    }

    if (ORCM_SUCCESS != (erri = opal_hash_table_init(LOGICAL_GROUP.groups, HASH_SIZE))) {
        return erri;
    }

    logical_group_file_lock.file_lock.l_pid = getpid();

    return erri;
}

int orcm_logical_group_finalize()
{
    if (NULL != LOGICAL_GROUP.groups) {
        opal_hash_table_remove_all(LOGICAL_GROUP.groups);
        OBJ_RELEASE(LOGICAL_GROUP.groups);
    }

    SAFEFREE(LOGICAL_GROUP.storage_filename);
    SAFEFREE(current_tag);

    return ORCM_SUCCESS;
}

static int orcm_logical_group_hash_table_get(opal_hash_table_t *groups, char *tag,
                                             opal_list_t **o_group_members)
{
    int erri = opal_hash_table_get_value_ptr(groups, tag, strlen(tag) + 1,
                                             (void**)o_group_members);
    if (OPAL_ERR_NOT_FOUND == erri || NULL == *o_group_members) {
        *o_group_members = OBJ_NEW(opal_list_t);
        if (NULL == *o_group_members) {
            return ORCM_ERR_OUT_OF_RESOURCE;
        }
    }

    return ORCM_SUCCESS;
}

static bool member_exist(opal_list_t *group_members, char *new_member)
{
    bool exist = false;
    orcm_logical_group_member_t *member_item = NULL;

    OPAL_LIST_FOREACH(member_item, group_members, orcm_logical_group_member_t) {
        if (NULL == member_item) {
            break;
        }
        if (0 == strncmp(member_item->member, new_member, strlen(member_item->member) + 1)) {
            exist = true;
            break;
        }
    }

    return exist;
}

static int orcm_logical_group_list_append(char *member, opal_list_t *member_list)
{
    int erri = ORCM_SUCCESS;

    orcm_logical_group_member_t *member_item = OBJ_NEW(orcm_logical_group_member_t);
    if (NULL == member_item) {
        return ORCM_ERR_OUT_OF_RESOURCE;
    }
    if (NULL == (member_item->member = strdup(member))) {
        OBJ_RELEASE(member_item);
        return ORCM_ERR_OUT_OF_RESOURCE;
    }

    opal_list_append(member_list, &member_item->super);

    return erri;
}

static int orcm_logical_group_add_internal(char *tag, char **new_members,
                                           opal_list_t *group_members,
                                           opal_hash_table_t *io_groups)
{
    int index = -1;
    int erri = ORCM_SUCCESS;
    int count = opal_argv_count(new_members);

    for (index = 0; index < count; index++) {
        if (false == member_exist(group_members, new_members[index])) {
            erri = orcm_logical_group_list_append(new_members[index], group_members);
            if (ORCM_SUCCESS != erri) {
                break;
            }
        }
    }

    if (erri == ORCM_SUCCESS) {
        erri = opal_hash_table_set_value_ptr(io_groups, tag, strlen(tag) + 1, group_members);
    }

    return erri;
}

int orcm_logical_group_add(char *tag, char *regex, opal_hash_table_t *io_groups)
{
    int erri = ORCM_SUCCESS;
    char **new_members = NULL;
    opal_list_t *group_members = NULL;

    if (NULL == io_groups) {
        erri = ORCM_ERR_BAD_PARAM;
        return erri;
    }
    erri = orcm_logical_group_hash_table_get(io_groups, tag, &group_members);
    if (ORCM_SUCCESS != erri) {
        return erri;
    }

    erri = orte_regex_extract_node_names(regex, &new_members);
    if (ORTE_SUCCESS != erri) {
        goto cleanup;
    }

    erri = orcm_logical_group_add_internal(tag, new_members, group_members, io_groups);

cleanup:
    opal_argv_free(new_members);
    if (ORCM_SUCCESS != erri && NULL != group_members) {
        OPAL_LIST_RELEASE(group_members);
    }
    return erri;
}

static bool is_do_all_wildcard(char *text)
{
    bool answer = false;
    if (NULL != text) {
        if(0 == strncmp(text, "*", strlen(text) + 1)) {
            answer = true;
        }
    }
    return answer;
}

static int orcm_logical_group_all_members_exist_one_tag(opal_list_t *value, char **members)
{
    int index = -1;
    bool exist = false;
    int count = opal_argv_count(members);

    for (index = 0; index < count; index++) {
        if (false == (exist = member_exist(value, members[index]))) {
            return ORCM_ERR_NODE_NOT_EXIST;
        }
    }

    return ORCM_SUCCESS;
}

static int orcm_logical_group_all_members_exist_all_tags(opal_hash_table_t *groups,
                                                         char **members)
{
    int erri = ORCM_SUCCESS;
    char *key = NULL;
    size_t key_size = 0;
    opal_list_t *value = NULL;
    void *in_member = NULL;
    void *out_member = NULL;

    while (OPAL_SUCCESS == opal_hash_table_get_next_key_ptr(groups, (void**)&key,
                                         &key_size, (void**)&value, in_member, &out_member)) {
            if (ORCM_SUCCESS !=
                (erri = orcm_logical_group_all_members_exist_one_tag(value, members))) {
                return erri;
            }
            in_member = out_member;
            out_member = NULL;
    }

    return erri;
}

static int orcm_logical_group_hash_table_remove_members(char *tag,
                                                        opal_list_t *value,
                                                        char **members,
                                                        opal_hash_table_t *io_groups)
{
    int index = -1;
    orcm_logical_group_member_t *member_item = NULL;
    orcm_logical_group_member_t *next_member_item = NULL;
    int count = opal_argv_count(members);

    for (index = 0; index < count; index++) {
        OPAL_LIST_FOREACH_SAFE(member_item, next_member_item, value, orcm_logical_group_member_t) {
            if (0 == strncmp(members[index], member_item->member, strlen(members[index]) + 1)) {
                opal_list_remove_item(value, &member_item->super);
                OBJ_RELEASE(member_item);
                break;
            }
        }
    }

    if (opal_list_is_empty(value)) {
        return opal_hash_table_remove_value_ptr(io_groups, tag, strlen(tag) + 1);
    }

    return opal_hash_table_set_value_ptr(io_groups, tag, strlen(tag) + 1, value);
}

static int orcm_logical_group_remove_all_tags(char **members, int do_all_member,
                                              opal_hash_table_t *io_groups)
{
    int erri = ORCM_SUCCESS;
    char *key = NULL;
    size_t key_size = 0;
    opal_list_t *value = NULL;
    void *in_member = NULL;
    void *out_member = NULL;

    if (do_all_member) {
        return opal_hash_table_remove_all(io_groups);
    }

    if (ORCM_SUCCESS !=
        (erri = orcm_logical_group_all_members_exist_all_tags(io_groups, members))) {
        return erri;
    }

    while (OPAL_SUCCESS == opal_hash_table_get_next_key_ptr(io_groups, (void**)&key,
                                         &key_size, (void**)&value, in_member, &out_member)) {
            erri = orcm_logical_group_hash_table_remove_members(key, value, members, io_groups);
            if (ORCM_SUCCESS != erri) {
                return erri;
            }
            in_member = out_member;
            out_member = NULL;
    }

    return erri;
}

static int orcm_logical_group_remove_a_tag(char *tag, char **members, int do_all_member,
                                           opal_hash_table_t *io_groups)
{
    int erri = ORCM_SUCCESS;
    opal_list_t *value = NULL;

    if (OPAL_SUCCESS == (erri = opal_hash_table_get_value_ptr(io_groups, tag,
                                                      strlen(tag) + 1, (void**)&value))) {
        if (do_all_member) {
            return opal_hash_table_remove_value_ptr(io_groups, tag, strlen(tag) + 1);
        }
        if (ORCM_SUCCESS != (erri = orcm_logical_group_all_members_exist_one_tag(value, members))) {
            return erri;
        }
        return orcm_logical_group_hash_table_remove_members(tag, value, members, io_groups);
    }

    if (OPAL_ERR_NOT_FOUND == erri) {
        return ORCM_ERR_GROUP_NOT_EXIST;
    }

    return erri;
}

static int
orcm_logical_group_remove_internal(char *tag, int do_all_tag, char **members,
                                   int do_all_member, opal_hash_table_t *io_groups)
{
    if (do_all_tag) {
        return orcm_logical_group_remove_all_tags(members, do_all_member, io_groups);
    }

    return orcm_logical_group_remove_a_tag(tag, members, do_all_member, io_groups);
}

int orcm_logical_group_remove(char *tag, char *regex, opal_hash_table_t *io_groups)
{
    int erri = ORCM_SUCCESS;
    int do_all_tag = 0;
    int do_all_member = 0;
    char **members = NULL;

    if (NULL == io_groups) {
        return ORCM_ERR_BAD_PARAM;
    }

    if (0 == opal_hash_table_get_size(io_groups)) {
        return ORCM_ERR_NO_ANY_GROUP;
    }

    do_all_tag = is_do_all_wildcard(tag);
    if (0 == (do_all_member = is_do_all_wildcard(regex))) {
        erri = orte_regex_extract_node_names(regex, &members);
        if (ORTE_SUCCESS != erri) {
            goto cleanup;
        }
    }

    erri = orcm_logical_group_remove_internal(tag, do_all_tag, members, do_all_member, io_groups);

cleanup:
    opal_argv_free(members);

    return erri;
}

static opal_list_t *
orcm_logical_group_list_specific_members(opal_list_t *value, char **members)
{
    int index = -1;
    orcm_logical_group_member_t *member_item = NULL;
    opal_list_t *new_value = NULL;
    int count = opal_argv_count(members);

    if (0 == count) {
        return NULL;
    }

    new_value = OBJ_NEW(opal_list_t);
    if (NULL != new_value) {
        for (index = 0; index < count; index++) {
            OPAL_LIST_FOREACH(member_item, value, orcm_logical_group_member_t) {
                if (NULL == member_item) {
                    return NULL;
                }
                if (0 == strncmp(members[index], member_item->member, strlen(members[index]) + 1)) {
                    orcm_logical_group_list_append(member_item->member, new_value);
                    break;
                }
            }
        }

        if (opal_list_is_empty(new_value)) {
            OPAL_LIST_RELEASE(new_value);
        }
    }

    return new_value;
}

static opal_hash_table_t*
orcm_logical_group_list_all_tags(char **members, int do_all_member, opal_hash_table_t *groups)
{
    opal_hash_table_t *o_groups = NULL;
    char *key = NULL;
    size_t key_size = 0;
    opal_list_t *value = NULL;
    opal_list_t *new_value = NULL;
    void *in_member = NULL;
    void *o_member = NULL;

    if (do_all_member) {
        return groups;
    }

    o_groups = OBJ_NEW(opal_hash_table_t);
    if (NULL != o_groups) {
        opal_hash_table_init(o_groups, HASH_SIZE);
        while (OPAL_SUCCESS == opal_hash_table_get_next_key_ptr(groups, (void**)&key,
                                           &key_size, (void**)&value, in_member, &o_member)) {
            new_value = orcm_logical_group_list_specific_members(value, members);
            if (NULL != new_value) {
                opal_hash_table_set_value_ptr(o_groups, key, key_size, new_value);
            }
            in_member = o_member;
            o_member = NULL;

        }
    }

    return o_groups;
}

static opal_hash_table_t *orcm_logical_group_list_a_tag(char *tag, char **members,
                                                        int do_all_member,
                                                        opal_hash_table_t *groups)
{
    opal_hash_table_t *o_groups = NULL;
    opal_list_t *value = NULL;
    opal_list_t *new_value = NULL;

    if (OPAL_SUCCESS == opal_hash_table_get_value_ptr(groups, tag,
                                                      strlen(tag) + 1, (void**)&value)) {
        o_groups = OBJ_NEW(opal_hash_table_t);
        if (NULL != o_groups) {
            opal_hash_table_init(o_groups, HASH_SIZE);
            if (do_all_member) {
                opal_hash_table_set_value_ptr(o_groups, tag, strlen(tag) + 1, value);
            } else {
                new_value = orcm_logical_group_list_specific_members(value, members);
                if (NULL != new_value) {
                    opal_hash_table_set_value_ptr(o_groups, tag, strlen(tag) + 1, new_value);
                }
            }
        }
    }

    return o_groups;
}

static opal_hash_table_t *
orcm_logical_group_list_internal(char *tag, int do_all_tag, char **members,
                                 int do_all_member, opal_hash_table_t *groups)
{
    if (do_all_tag) {
        return orcm_logical_group_list_all_tags(members, do_all_member, groups);
    }

    return orcm_logical_group_list_a_tag(tag, members, do_all_member, groups);
}

opal_hash_table_t *orcm_logical_group_list(char *tag, char *regex,
                                           opal_hash_table_t *groups)
{
    int do_all_tag = 0;
    int do_all_member = 0;
    char **members = NULL;
    opal_hash_table_t *o_groups = NULL;

    if (NULL == groups) {
        return NULL;
    }

    do_all_tag = is_do_all_wildcard(tag);
    if (0 == (do_all_member = is_do_all_wildcard(regex))) {
        if (ORTE_SUCCESS != orte_regex_extract_node_names(regex, &members)) {
            goto cleanup;
        }
    }

    o_groups = orcm_logical_group_list_internal(tag, do_all_tag,
                                                members, do_all_member, groups);

cleanup:
    opal_argv_free(members);
    return o_groups;
}

static int orcm_logical_group_is_comment(char *line)
{
    if (NULL == line || '\0' == line[0] || '\n' == line[0] ||
        '\r' == line[0] || '#'  == line[0]
       ) {
        return 1;
    }

    return 0;
}

static int orcm_logical_group_split_line(char *line, char ***o_line_fields)
{
    *o_line_fields = opal_argv_split(line, '=');
    if (2 != opal_argv_count(*o_line_fields)) {
        return ORCM_ERR_BAD_PARAM;
    }

    return ORCM_SUCCESS;
}

static int orcm_logical_group_process_tag_line(char *tag)
{
    if (NULL == current_tag || (0 != strncmp(current_tag, tag, strlen(current_tag) + 1))) {
        SAFEFREE(current_tag);
        current_tag = strdup(tag);
    }

    return ORCM_SUCCESS;
}

static int orcm_logical_group_process_line(char *line, opal_hash_table_t *groups)
{
    int erri = ORCM_SUCCESS;
    char **line_fields = NULL;

    if (orcm_logical_group_is_comment(line)) {
        return erri;
    }

    if (ORCM_SUCCESS != (erri = orcm_logical_group_split_line(line, &line_fields))) {
        goto cleanup;
    }

    if (0 == strncmp(line_fields[0], "group name", strlen(line_fields[0]) + 1)) {
        erri = orcm_logical_group_process_tag_line(line_fields[1]);
    } else if (0 == strncmp(line_fields[0], "member list", strlen(line_fields[0]) + 1)) {
        erri = orcm_logical_group_add(current_tag, line_fields[1], groups);
    } else {
        ORCM_UTIL_ERROR_MSG_WITH_ARG("Not recognize the current line: %s", line);
        erri = ORCM_ERR_BAD_PARAM;
    }

cleanup:
    opal_argv_free(line_fields);
    return erri;
}

static void orcm_logical_group_trim_string(char *string, char **o_string)
{
    char *in_string_travesal = NULL;

    if (NULL == string || NULL == o_string) {
        return;
    }

    /* trim the beginning */
    while(' ' == *string || '\t' == *string) {
        string++;
    }
    *o_string = string;

    /* trim the end */
    if ('\0' != *string) {
        in_string_travesal = string + strlen(string) - 1;
        while (in_string_travesal > string &&
               (' ' == *in_string_travesal || '\t' == *in_string_travesal)) {
            *in_string_travesal = '\0';
            in_string_travesal--;
        }
    }
}

static int orcm_logical_group_get_newline(FILE *storage_file, char *io_line,
                                          int max_line_length, int *o_eof)
{
    char *ret = NULL;
    char *line_break = NULL;

    /* +2 means to include the last '\0' and the line break '\n' */
    ret = fgets(io_line, max_line_length + 2, storage_file);
    if (NULL == ret) {
        if (0 != feof(storage_file)) {
            *o_eof = 1;
            return ORCM_SUCCESS;
        }
        return ORCM_ERR_FILE_READ_FAILURE;
    }

    /* trim the last '\n' introduced by line break */
    if (NULL != (line_break = strchr(io_line, '\n'))) {
        *line_break = '\0';
    }

    return ORCM_SUCCESS;
}

static int orcm_logical_group_parsing(char *line_buf, opal_hash_table_t *io_groups)
{
    int eof = -1;
    int erri = -1;
    char *line_buf_after_trim = NULL;

    if (-1 == fcntl(logical_group_file_lock.fd, F_SETLKW,
                    &(logical_group_file_lock.file_lock))) {
        return ORCM_ERR_FILE_READ_FAILURE;
    }

    while (1) {
        eof = 0;
        memset(line_buf, '\0', strlen(line_buf));
        erri = orcm_logical_group_get_newline(logical_group_file_lock.file, line_buf,
                                              MAX_LINE_LENGTH, &eof);
        if (ORCM_SUCCESS != erri || 1 == eof) {
            break;
        }

        if (orcm_logical_group_is_comment(line_buf)) {
            continue;
        }

        orcm_logical_group_trim_string(line_buf, &line_buf_after_trim);
        erri = orcm_logical_group_process_line(line_buf_after_trim, io_groups);
        if (ORCM_SUCCESS != erri) {
            break;
        }
    }

    return erri;
}

static int orcm_logical_group_parse_from_file(opal_hash_table_t *io_groups)
{
    char *line_buf = NULL;
    int erri = -1;

    if (1 >= MAX_LINE_LENGTH) {
        ORCM_UTIL_ERROR_MSG("The line length needs to be set larger than 1");
        return ORCM_ERR_BAD_PARAM;
    }

    /* +2 means adding '\0' in the end, and '\n' line break */
    if (NULL == (line_buf = (char*)malloc((MAX_LINE_LENGTH + 2) * sizeof(char)))) {
        ORCM_UTIL_ERROR_MSG("Failed to allocate line buffer for logical groupings.");
        return ORCM_ERR_OUT_OF_RESOURCE;
    }

    erri = orcm_logical_group_parsing(line_buf, io_groups);

    SAFEFREE(line_buf);

    return erri;
}

static FILE *orcm_logical_group_open_file(char *storage_filename,
                                          char *mode, bool *o_file_missing)
{
    FILE *storage_file = NULL;

    if (NULL == storage_filename || '\0' == storage_filename[0]) {
        ORCM_UTIL_ERROR_MSG("Bad setup for parsing logical groupings.");
    } else {
        if (NULL == (storage_file = fopen(storage_filename, mode))) {
            if (ENOENT == errno) {
                *o_file_missing = true;
            } else {
                ORCM_UTIL_ERROR_MSG("Failed to open file for logical groupings.");
            }
        }
    }

    return storage_file;
}

static int orcm_logical_group_close_file(void)
{
    int erri = ORCM_SUCCESS;

    fflush(logical_group_file_lock.file);
    logical_group_file_lock.file_lock.l_type = F_UNLCK;
    if (-1 == fcntl(logical_group_file_lock.fd, F_SETLK,
                    &(logical_group_file_lock.file_lock))) {
        erri = ORCM_ERR_FILE_READ_FAILURE;
    }
    fclose(logical_group_file_lock.file);

    return erri;
}

static int orcm_logical_group_load_from_file(char *storage_filename,
                                             opal_hash_table_t *io_groups)
{
    bool file_missing = false;
    char *mod = "r";
    int erri = ORCM_SUCCESS;
    int ret = ORCM_SUCCESS;

    if (NULL == io_groups) {
        ORCM_UTIL_ERROR_MSG("Missing logical group.");
        return ORCM_ERR_BAD_PARAM;
    }

    logical_group_file_lock.file_lock.l_type = F_RDLCK;
    logical_group_file_lock.file = orcm_logical_group_open_file(storage_filename,
                                                                mod, &file_missing);
    if (NULL == logical_group_file_lock.file) {
        if (file_missing) {
            return ORCM_SUCCESS;
        }
        return ORCM_ERR_FILE_OPEN_FAILURE;
    }
    logical_group_file_lock.fd = fileno(logical_group_file_lock.file);

    erri = orcm_logical_group_parse_from_file(io_groups);
    ret = orcm_logical_group_close_file();
    if (ORCM_SUCCESS == erri) {
        return ret;
    }

    return erri;
}

static opal_list_t *orcm_logical_group_do_convertion(opal_list_t *members_list,
                                                     char *memberlist,
                                                     unsigned int reserved_size)
{
    orcm_logical_group_member_t *member_item = NULL;
    unsigned int current_size = 0;
    int index = 0;
    int count = opal_list_get_size(members_list);
    opal_list_t *new_members_list = OBJ_NEW(opal_list_t);
    if (NULL == new_members_list) {
        return NULL;
    }

    OPAL_LIST_FOREACH(member_item, members_list, orcm_logical_group_member_t) {
        index++;
        current_size = strlen(memberlist);
        if (reserved_size < current_size + strlen(member_item->member) + 1) {
            if (ORCM_SUCCESS != orcm_logical_group_list_append(memberlist, new_members_list)) {
                goto clean;
            }
            memset(memberlist, '\0', strlen(memberlist));
        }
        if (0 < strlen(memberlist)) {
            strncat(memberlist, ",", sizeof(char));
        }
        strncat(memberlist, member_item->member, strlen(member_item->member));

        if (index == count && 0 < strlen(memberlist)) {
            if (ORCM_SUCCESS != orcm_logical_group_list_append(memberlist, new_members_list)) {
                goto clean;
            }
        }
    }

    return new_members_list;

clean:
    OPAL_LIST_RELEASE(new_members_list);
    return NULL;
}

opal_list_t *orcm_logical_group_convert_members_list(opal_list_t *members_list,
                                                     unsigned int max_size)
{
    char *memberlist = NULL;
    opal_list_t *o_members_list = NULL;

    if (NULL == members_list || opal_list_is_empty(members_list) || 0 >= max_size) {
        return NULL;
    }

    memberlist = (char*)calloc(max_size, sizeof(char));
    if (NULL == memberlist) {
        return NULL;
    }

    o_members_list = orcm_logical_group_do_convertion(members_list, memberlist, max_size);
    SAFEFREE(memberlist);
    return o_members_list;
}

static int orcm_logical_group_save_to_file_concat(char *tag, opal_list_t *members_list)
{
    int erri = ORCM_SUCCESS;
    orcm_logical_group_member_t *regex = NULL;
    opal_list_t *new_members_list = orcm_logical_group_convert_members_list(members_list,
                                                MAX_LINE_LENGTH - strlen("member list="));

    if (NULL == tag || NULL == new_members_list) {
        return erri;
    }

    if (0 > fprintf(logical_group_file_lock.file, "group name=%s\n", tag)) {
        erri = ORCM_ERR_FILE_WRITE_FAILURE;
        goto cleanup;
    }

    OPAL_LIST_FOREACH(regex, new_members_list, orcm_logical_group_member_t) {
        if (NULL == regex) {
            erri = ORCM_ERR_BAD_PARAM;
            goto cleanup;
        }
        if (0 > fprintf(logical_group_file_lock.file, "member list=%s\n", regex->member)) {
            erri = ORCM_ERR_FILE_WRITE_FAILURE;
            goto cleanup;
        }
    }

cleanup:
    if (NULL != new_members_list) {
        OPAL_LIST_RELEASE(new_members_list);
    }
    return erri;
}

static int orcm_logical_group_save_to_file_internal(opal_hash_table_t *groups)
{
    int erri = ORCM_SUCCESS;
    int ret = 0;
    char *key = NULL;
    size_t key_size = 0;
    opal_list_t *value = NULL;
    void *in_member = NULL;
    void *out_member = NULL;

    if (-1 == fcntl(logical_group_file_lock.fd, F_SETLKW,
                    &(logical_group_file_lock.file_lock))) {
        return ORCM_ERR_FILE_READ_FAILURE;
    }
    if (0 == opal_hash_table_get_size(groups)) {
        ret = fprintf(logical_group_file_lock.file, "# Empty logical grouping set\n");
        if (0 > ret) {
            erri = ORCM_ERR_FILE_WRITE_FAILURE;
        }
    } else {
        while (ORCM_SUCCESS == opal_hash_table_get_next_key_ptr(groups, (void**)&key,
                                        &key_size, (void**)&value, in_member, &out_member)) {
            erri = orcm_logical_group_save_to_file_concat(key, value);
            if (ORCM_SUCCESS != erri) {
                break;
            }
            in_member = out_member;
            out_member = NULL;
        }
    }

    return erri;
}

int orcm_logical_group_save_to_file(char *storage_filename, opal_hash_table_t *groups)
{
    bool file_missing = false;
    char *mod = "w";
    int erri = ORCM_SUCCESS;
    int ret = ORCM_SUCCESS;

    if (NULL == groups) {
        ORCM_UTIL_ERROR_MSG("Missing logical group.");
        return ORCM_ERR_BAD_PARAM;
    }

    logical_group_file_lock.file_lock.l_type = F_WRLCK;
    logical_group_file_lock.file = orcm_logical_group_open_file(storage_filename,
                                                                mod, &file_missing);
    if (NULL == logical_group_file_lock.file) {
        return ORCM_ERR_FILE_OPEN_FAILURE;
    }
    logical_group_file_lock.fd = fileno(logical_group_file_lock.file);

    erri = orcm_logical_group_save_to_file_internal(groups);
    ret = orcm_logical_group_close_file();
    if (ORCM_SUCCESS == erri) {
        return ret;
    }

    return erri;
}

static int orcm_logical_group_tag_to_members_nested(char ***tag_array, char ***o_array_string)
{
    int count = -1;
    int erri = ORCM_SUCCESS;
    orcm_logical_group_member_t *tag_member = NULL;
    char *tag = NULL;
    opal_list_t *value = NULL;
    opal_list_t *nested_value = NULL;

    count = opal_argv_count(*tag_array);
    while (0 < count) {
        tag = (*tag_array)[0];
        if (ORCM_SUCCESS == (opal_hash_table_get_value_ptr(LOGICAL_GROUP.groups,
                             (void*)tag, strlen(tag) + 1, (void**)&value))) {
            if (NULL != value) {
                OPAL_LIST_FOREACH(tag_member, value, orcm_logical_group_member_t) {
                    if (NULL == tag_member) {
                        continue;
                    }
                    if (ORCM_SUCCESS == (opal_hash_table_get_value_ptr(LOGICAL_GROUP.groups,
                        (void*)tag_member->member, strlen(tag_member->member) + 1,
                        (void**)&nested_value))) {
                        erri = opal_argv_append_unique_nosize(tag_array,
                                                              tag_member->member, false);
                        if (OPAL_SUCCESS != erri) {
                            return erri;
                        }
                        count++;
                    } else {
                        erri = opal_argv_append_unique_nosize(o_array_string,
                                                              tag_member->member, false);
                        if (OPAL_SUCCESS != erri) {
                            return erri;
                        }
                    }
                }
            }
        }
        if (OPAL_SUCCESS != (erri = opal_argv_delete(&count, tag_array, 0, 1))) {
            return erri;
        }
    }

    return erri;
}

int orcm_logical_group_parse_array_string(char *regex, char ***o_array_string)
{
    int erri = ORCM_SUCCESS;
    int size = 0;
    int index = 0;
    char *o_regex = NULL;
    char **regex_array = NULL;
    char **tag_array = NULL;

    orcm_logical_group_trim_string(regex, &o_regex);
    if (NULL == o_regex || '\0' == *o_regex) {
        return erri;
    }

    if (ORCM_SUCCESS != (erri = (orte_regex_extract_node_names(o_regex, &regex_array)))) {
        goto cleanup;
    }
    size = opal_argv_count(regex_array);
    for (index = 0; index < size; index++) {
        o_regex = regex_array[index];
        if ('$' == *(regex_array[index])) {
            o_regex++;
            erri = opal_argv_append_unique_nosize(&tag_array, o_regex, false);
            if (OPAL_SUCCESS != erri) {
                goto cleanup;
            }
        } else {
            erri = opal_argv_append_unique_nosize(o_array_string, o_regex, false);
            if (OPAL_SUCCESS != erri) {
                goto cleanup;
            }
        }
    }
    erri = orcm_logical_group_tag_to_members_nested(&tag_array, o_array_string);

cleanup:
    opal_argv_free(regex_array);
    opal_argv_free(tag_array);
    if (ORCM_SUCCESS != erri) {
        opal_argv_free(*o_array_string);
    }
    return erri;
}

static unsigned int orcm_logical_group_argv_addup_size(char **argv, int *count)
{
    int index = 0;
    unsigned int size = 0;

    *count = opal_argv_count(argv);
    for (; index < *count; index++) {
        size += (strlen(argv[index]) + 1);
    }

    return size;
}

static int orcm_logical_group_arraystring_to_string(char **array_string, char **string)
{
    unsigned int size = 0;
    int erri = ORCM_SUCCESS;
    int index = 0;
    int count = 0;

    if (0 == (size = orcm_logical_group_argv_addup_size(array_string, &count))) {
        return ORCM_ERR_BAD_PARAM;
    }

    if (NULL == (*string = (char*)calloc(size, sizeof(char)))) {
        return ORCM_ERR_OUT_OF_RESOURCE;
    }

    for (; index < count; index++) {
        strncat(*string, array_string[index], strlen(array_string[index]));
        if (index != (count - 1)) {
            strncat(*string, ",", sizeof(char));
        }
    }

    return erri;
}

int orcm_logical_group_parse_string(char *regex, char **o_string)
{
    int erri = ORCM_SUCCESS;
    char **o_string_array = NULL;

    erri = orcm_logical_group_parse_array_string(regex, &o_string_array);
    if (ORCM_SUCCESS != erri) {
        return erri;
    }

    erri = orcm_logical_group_arraystring_to_string(o_string_array, o_string);
    opal_argv_free(o_string_array);

    return erri;
}

int orcm_logical_group_load_to_memory(char *config_file)
{
    int erri = ORCM_SUCCESS;
    if (ORCM_SUCCESS != (erri = orcm_logical_group_init(config_file))) {
        return erri;
    }

    return orcm_logical_group_load_from_file(LOGICAL_GROUP.storage_filename,
                                             LOGICAL_GROUP.groups);
}
