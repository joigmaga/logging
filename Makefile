#
#  Standard g++ compile
#
CXX           := g++
CXXFLAGS      := -I ./include -std=c++11
CXXEXTRAFLAGS := -Wall -Werror
LDFLAGS       := -pthread

# what to do

SOURCES	        := logging.cpp formatting.cpp handling.cpp
OBJECTS	        := ${SOURCES:.cpp=.o} 

PROGRAMS        := test test2 copytest tt thr
PROGRAM_SOURCES := ${PROGRAMS:=.cpp}
PROGRAM_OBJECTS := ${PROGRAMS:=.o}

INCLUDE_FILES   := include/logging.h

# rules
#
# $@  target name
# $?  prerequisites newer than target
# $^  all prerequisites
# $<  first prerequisite

#  Putting everything together 
#
.PHONY: all

all: ${PROGRAMS}

${PROGRAMS}: % : %.o ${OBJECTS} ${INCLUDE_FILES} Makefile
	${CXX} ${LDFLAGS} $< ${OBJECTS} -o $@

# generic object compilation
#
%.o: %.cpp ${INCLUDE_FILES} Makefile
	${CXX} ${CXXFLAGS} ${CXXEXTRAFLAGS} -c $<

# cleanup
#
clean-objects:
	rm -f ${PROGRAM_OBJECTS} ${OBJECTS}

clean:
	rm -f ${PROGRAMS} ${PROGRAM_OBJECTS} ${OBJECTS}

