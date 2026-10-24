# @file check.cmake
# @brief Verify immutable upstream and independent qualification fixture hashes.
# @author watson
cmake_minimum_required(VERSION 3.28)
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
function(check_fixture path expected)
  file(SHA256 "${root}/${path}" actual)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Fixture changed without a provenance update: ${path}")
  endif()
endfunction()
check_fixture(third_party/vallado/SGP4.cpp
  7e984e29a66f75a2b5ad3881f0e5dc0f8fbf9ee8de675ee39be1c9092bc3f183)
check_fixture(third_party/vallado/SGP4.h
  0563e6e8027607452e451d2342da96e5b376a1c119675c40940866f2e04c0bd6)
check_fixture(third_party/vallado/NOTICE
  3b90f5a746be2899b444e3794e74cf13ffe6183ea32f52498ba16522e5ca56bd)
check_fixture(tests/data/SGP4-VER.TLE
  d246d1d9d768ace445a38a965713fa9ba52d80fd8a41a0502ff83d7acffe2881)
check_fixture(tests/data/tcppver.out
  687bf28dbe52df86e8e60ab5cb4a08d1aa3dbcaf4e63b1f7ab95f044fbe3833b)
check_fixture(tests/data/python-sgp4-LICENSE
  4474ea9eccbb829c6bc3652381bfe78b1c815de021d4a284a2a5b383a374d030)
check_fixture(tests/data/independent/iss-oem-20261021.txt
  7c4f78b7f3e4bb5fafc210ab29f4e80bdae6dd66c84e27e8f47cd8f9e7976eb7)
check_fixture(tests/data/independent/iss-teme.csv
  0e1fe5e97b2a95737b8bcc5e77c3607347d4dd8bb95af264b8d66f719020e8a8)
check_fixture(tests/data/independent/leap-seconds.list
  db5a895f16853b03bfc865e8d68f9fc8710ef1740e3400c701cd46a5bbbc3433)
check_fixture(tests/data/independent/iss-6h-6p.tle
  0c0bbca7ffb1c8114041b83fbde3b3c0bc38081db0410cc892537ba17d4bbb6f)
check_fixture(tests/data/independent/iss-6h-7p.tle
  0a3306f5b61aa1abd20d4c202db7eaa3508fd458225b1417260b3652fb651b24)
check_fixture(tests/data/independent/iss-12h-6p.tle
  770e6577bf97afb6bfdce83321410398f5437ac04ed558382f789e229136d3ef)
check_fixture(tests/data/independent/iss-12h-7p.tle
  f0de5b905b91c928cbde5c2e12c27757bc84a2f9ce071d59c43589d63925dd7e)
check_fixture(tests/data/independent/iss-24h-6p.tle
  96cd51558882f0eca3c27b5dcbcf72fe2239b837a1143d8cd58bedcb800706aa)
check_fixture(tests/data/independent/iss-24h-7p.tle
  2d3f040cfc55b16c654c161e4c031aa712b8e1efca6b0372e634363ebaa6357a)
check_fixture(tests/data/independent/iss-48h-6p.tle
  a76c454e8b80dbce93b6d3041dd96b4cb1d95cd9136c440120a07741218fdde2)
check_fixture(tests/data/independent/iss-48h-7p.tle
  81532edbf5c65d640c823d919de57a7c69d0a399d50c05d7802437f00f2ae589)
message(STATUS "Verified 17 immutable source and fixture hashes")
