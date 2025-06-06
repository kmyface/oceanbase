
# charset objects used for proxy_parser



set(ob_sql_parser_charset_object_list
    ob_ctype_bin.cc
    ob_ctype.cc
    ob_ctype_gbk_tab.cpp
    ob_ctype_gbk.cc
    ob_ctype_latin1_tab.cpp
    ob_ctype_latin1.cc
    ob_ctype_ascii_tab.cpp
    ob_ctype_gb2312_tab.cpp
    ob_ctype_gb2312.cc
    ob_ctype_ujis_tab.cpp
    ob_ctype_ujis.cc
    ob_ctype_euckr_tab.cpp
    ob_ctype_euckr.cc
    ob_ctype_eucjpms_tab.cpp
    ob_ctype_eucjpms.cc
    ob_ctype_cp932_tab.cpp
    ob_ctype_cp932.cc
    ob_ctype_ascii.cc
    ob_ctype_tis620_tab.cpp
    ob_ctype_tis620.cc
    ob_ctype_mb.cc
    ob_ctype_simple.cc
    ob_ctype_utf8.cc
    ob_dtoa.cc
    ob_ctype_utf16.cc
    ob_ctype_uca.cc
    ob_ctype_gb18030_tab.cpp
    ob_ctype_gb18030.cc
    uca900_ja_tbls.cc
    uca900_zh_tbls.cc
    uca900_zh2_tbls.cc
    uca900_zh3_tbls.cc
    )

list(TRANSFORM ob_sql_parser_charset_object_list
     PREPEND ${PROJECT_SOURCE_DIR}/deps/oblib/src/lib/charset/)
# hash objects used for proxy parser
set(ob_sql_parser_hash_object_list
  murmur_hash.h
  murmur_hash.cpp
)

list(TRANSFORM ob_sql_parser_hash_object_list
  PREPEND ${PROJECT_SOURCE_DIR}/deps/oblib/src/lib/hash_func/)

add_library(ob_sql_parser_base INTERFACE)
target_include_directories(ob_sql_parser_base INTERFACE
  ${PROJECT_SOURCE_DIR}/deps/oblib/src/common/sql_mode/
  ${PROJECT_SOURCE_DIR}/src/objit/include/objit/common
  ${CMAKE_SOURCE_DIR}/deps/oblib/src/lib/charset/
  )

# parser objects for proxy parser
if (OB_BUILD_ORACLE_PARSER)
  set(ob_inner_sql_parser_object_list
    sql_parser_oracle_utf8_mode_lex.c
    sql_parser_oracle_utf8_mode_lex.h
    sql_parser_oracle_utf8_mode_tab.c
    sql_parser_oracle_utf8_mode_tab.h
    sql_parser_oracle_gbk_mode_lex.c
    sql_parser_oracle_gbk_mode_lex.h
    sql_parser_oracle_gbk_mode_tab.c
    sql_parser_oracle_gbk_mode_tab.h
    sql_parser_oracle_single_byte_mode_lex.c
    sql_parser_oracle_single_byte_mode_lex.h
    sql_parser_oracle_single_byte_mode_tab.c
    sql_parser_oracle_single_byte_mode_tab.h
    sql_parser_oracle_hkscs_mode_lex.c
    sql_parser_oracle_hkscs_mode_tab.c
  )

  set(ob_inner_sql_parser_object_list
    ${ob_inner_sql_parser_object_list}
    non_reserved_keywords_oracle_utf8_mode.c
    non_reserved_keywords_oracle_gbk_mode.c
    non_reserved_keywords_oracle_single_byte_mode.c
    non_reserved_keywords_oracle_hkscs_mode.c
  )
endif()

# common
set(ob_inner_sql_parser_object_list
  ${ob_inner_sql_parser_object_list}
  sql_parser_mysql_mode_lex.c
  sql_parser_mysql_mode_lex.h
  sql_parser_mysql_mode_tab.c
  sql_parser_mysql_mode_tab.h
  ftsblex_lex.c
  ftsblex_lex.h
  ftsparser_tab.c
  ftsparser_tab.h
  fts_base.h
  fts_base.c
  fts_parse.h
  fts_parse.c
  non_reserved_keywords_mysql_mode.c
  ob_item_type_str.h
  ob_non_reserved_keywords.c
  ob_non_reserved_keywords.h
  ob_sql_parser.cpp
  ob_sql_parser.h
  parse_define.h
  parse_malloc.cpp
  parse_malloc.h
  parse_node.c
  parse_node.h
  parse_node_hash.cpp
  parse_node_hash.h
  ob_char_type.h
  ob_fast_parser.h
  ob_fast_parser.cpp
  ob_parser_charset_utils.cpp
  sql_parser_base.c
  sql_parser_base.h
  sql_parser_base.h
  parser_utility.cpp
  parser_utility.h
  type_name.c
)

# parser objects for server parser
set(ob_extra_sql_parser_object_list
  ob_parser.cpp
  ob_parser.h
  ob_parser_utils.h
  parser_proxy_func.cpp
  parser_proxy_func.h
)

# ob_sql_proxy_parser_objects is the static library for proxy, it does not link against observer
add_library(ob_sql_proxy_parser_objects OBJECT
            ${ob_inner_sql_parser_object_list}
            ${ob_sql_parser_hash_object_list}
            ${ob_sql_parser_charset_object_list}
            )
if (BUILD_CDC_ONLY)
  set_target_properties(ob_sql_proxy_parser_objects PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()


# ob_sql_server_parser_object is the static library for observer
add_library(ob_sql_server_parser_objects OBJECT
  ${ob_inner_sql_parser_object_list}
  ${ob_extra_sql_parser_object_list})


target_link_libraries(ob_sql_proxy_parser_objects PUBLIC ob_base ob_sql_parser_base)
target_link_libraries(ob_sql_server_parser_objects PUBLIC ob_base)

if (WITH_COVERAGE)
  # 排除少量sql解析文件，不参与coverage编译，clang针对于特殊语法文件会core
  config_remove_coverage_flag(ob_sql_proxy_parser_objects)
  config_remove_coverage_flag(ob_sql_server_parser_objects)
endif()

target_compile_definitions(ob_sql_proxy_parser_objects PUBLIC SQL_PARSER_COMPILATION)

add_library(ob_sql_proxy_parser_static
  STATIC
  EXCLUDE_FROM_ALL)


add_library(ob_sql_server_parser_static
  STATIC
  EXCLUDE_FROM_ALL)


target_link_libraries(ob_sql_proxy_parser_static
  PUBLIC
  ob_sql_proxy_parser_objects)

if(ENABLE_THIN_LTO AND USE_LTO_CACHE)
  add_dependencies(ob_sql_proxy_parser_static observer)
endif()

target_link_libraries(ob_sql_server_parser_static
  PUBLIC
  ob_sql_server_parser_objects)
