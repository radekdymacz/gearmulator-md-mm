cmake_minimum_required(VERSION 3.10)

project(dsp56kTestRunner)

add_executable(dsp56kTestRunner)

target_sources(dsp56kTestRunner PRIVATE testRunner.cpp)

target_link_libraries(dsp56kTestRunner PUBLIC dsp56kEmu)

add_test(NAME dsp56300_unitTests COMMAND dsp56kTestRunner)
set_tests_properties(dsp56300_unitTests PROPERTIES LABELS "UnitTest;dsp56300")

add_executable(dsp56kEssiOnDemandTests essiOnDemandTest.cpp)
target_link_libraries(dsp56kEssiOnDemandTests PRIVATE dsp56kEmu)
add_test(NAME dsp56300_essiOnDemandTests COMMAND dsp56kEssiOnDemandTests)
set_tests_properties(dsp56300_essiOnDemandTests PROPERTIES LABELS "UnitTest;dsp56300")

add_executable(dsp56kAccumulatorTests)

target_sources(dsp56kAccumulatorTests PRIVATE accumulatorTests.cpp manualAccumulatorTests.cpp)
target_link_libraries(dsp56kAccumulatorTests PRIVATE dsp56kEmu)
add_test(NAME dsp56300_accumulatorTests COMMAND dsp56kAccumulatorTests)
set_tests_properties(dsp56300_accumulatorTests PROPERTIES LABELS "UnitTest;dsp56300" TIMEOUT 120)
