aux_source_directory(. src_list)
add_library(fuse_extent.lib ${src_list})
target_link_libraries(fuse_extent.lib pthread)