TARGET=custom_mutator
PB_SRC_DIR=$(HOME)/libprotobuf-mutator-asn1/src

CXX=clang++-14
CXXFLAGS=-I$(PB_SRC_DIR)

AFL_DIR=$(HOME)/AFLplusplus
AFLCC=$(HOME)/AFLplusplus/afl-gcc

PROTOBUF_DIR=$(HOME)/libprotobuf-mutator/build/external.protobuf
PROTOBUF_LIB=$(PROTOBUF_DIR)/lib/libprotobufd.a

LPM_DIR=$(HOME)/libprotobuf-mutator
LPM_LIB=$(LPM_DIR)/build/src/libprotobuf-mutator.a

INC=-I$(PROTOBUF_DIR)/include -I$(LPM_DIR) -I$(AFL_DIR)/include

all: $(TARGET).so protobuf_to_der

$(TARGET).so: $(TARGET).cc $(PB_SRC_DIR)/*.cc
	$(CXX) $(CXXFLAGS) -fPIC -c $^ $(INC)
	$(CXX) -shared -Wall -O3 -o $@ *.o $(LPM_LIB) $(PROTOBUF_LIB)

protobuf_to_der: protobuf_to_der.cpp
	$(CXX) -o protobuf_to_der protobuf_to_der.cpp $(PB_SRC_DIR)/*.cc -I$(PB_SRC_DIR) $(INC) $(LPM_LIB) $(PROTOBUF_LIB)

.PHONY: clean
clean: 
	rm *.so *.o
