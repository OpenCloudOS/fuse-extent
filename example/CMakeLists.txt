add_executable(passthrough passthrough.c)
target_link_libraries(passthrough fuse_extent.lib)

add_executable(passthrough_cr passthrough_cr.c)
target_link_libraries(passthrough_cr fuse_extent.lib)