storage_unittest(test_bit_stream)
storage_unittest(test_micro_block_decoder)
storage_unittest(test_micro_block_encoder)

storage_unittest(test_bitset)
storage_unittest(test_hex)
storage_unittest(test_encoding_util)
storage_unittest(test_raw_decoder)
target_link_libraries(test_raw_decoder PUBLIC truncate_info_helper)
storage_unittest(test_const_decoder)
storage_unittest(test_general_column_decoder)
storage_unittest(test_column_equal_decoder)
