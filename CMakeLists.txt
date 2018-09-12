cmake_minimum_required (VERSION 2.6)
project (coherentsdr)
add_executable(coherentsdr coherentsdr.cc)
add_executable(controlcmd controlcmd.cc)

#gcc coherentsdr.cc -lzmq -lpthread -lfftw3f -lvolk -lstdc++ -lrtlsdr -lm -o coherentsdr
target_link_libraries(coherentsdr zmq pthread fftw3f volk stdc++ rtlsdr m)
target_link_libraries(controlcmd zmq stdc++)