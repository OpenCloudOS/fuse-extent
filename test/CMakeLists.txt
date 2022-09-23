find_package(PythonInterp REQUIRED)

exec_program(mkdir ${PROJECT_SOURCE_DIR}/fusedir)
exec_program(mkdir ${PROJECT_SOURCE_DIR}/fusedir/testdir)

# 测试参数解析（完成）
add_executable(fuse_option_test fuse_option_test.c)
target_link_libraries(fuse_option_test fuse_extent.lib)
add_test(OPTION_TEST ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/option_test.py ${PROJECT_SOURCE_DIR}/build/test/fuse_option_test)

# 测试信号机制（完成）
add_executable(fuse_signal_test fuse_signal_test.c)
target_link_libraries(fuse_signal_test fuse_extent.lib)
add_test(SIGNAL_TEST fuse_signal_test)

# 测试会话创建
add_executable(fuse_session_test fuse_session_test.c)
target_link_libraries(fuse_session_test fuse_extent.lib)
add_test(SESSION_TEST1 fuse_session_test)
add_test(SESSION_TEST2 fuse_session_test none1)
set_tests_properties (SESSION_TEST2 PROPERTIES PASS_REGULAR_EXPRESSION "none1")
add_test(SESSION_TEST3 fuse_session_test none1 none2)
set_tests_properties (SESSION_TEST3 PROPERTIES PASS_REGULAR_EXPRESSION "none1 none2")

add_executable(fuse_mount_test fuse_mount_test.c)
target_link_libraries(fuse_mount_test fuse_extent.lib)
add_test(MOUNT_TEST1 fuse_mount_test --allow_other)
add_test(MOUNT_TEST2 fuse_mount_test --auto_unmount)
add_test(MOUNT_TEST3 fuse_mount_test --fsname=haha)
add_test(MOUNT_TEST4 fuse_mount_test --subtype=haha)
add_test(MOUNT_TEST5 fuse_mount_test --flags=suid)
add_test(MOUNT_TEST6 fuse_mount_test --flags=ro,suid)
