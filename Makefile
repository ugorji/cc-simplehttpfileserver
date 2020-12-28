COMMON = $(CURDIR)/../cc-common

include $(COMMON)/Makefile.include.mk

objFiles = \
	$(BUILD)/simple_http_file_server_handler.o \
	$(BUILD)/simple_http_file_server_main.o \

all: .common.all $(objFiles) $(BUILD)/__simplehttpfileserver $(BUILD)/__simplehttpfileserver_static

clean:
	rm -f $(BUILD)/*

$(BUILD)/__simplehttpfileserver: $(commonObjFiles) $(objFiles) | $(BUILD)
	mkdir -p $(BUILD) && \
	$(CXX) -o $(BUILD)/__simplehttpfileserver $(commonObjFiles) $(objFiles) $(LDFLAGS)

$(BUILD)/__simplehttpfileserver_static: $(commonObjFiles) $(objFiles) | $(BUILD)
	mkdir -p $(BUILD) && \
	$(CXX) -o $(BUILD)/__simplehttpfileserver_static $(commonObjFiles) $(objFiles) $(LDFLAGS) -static -static-libgcc -static-libstdc++

server: $(BUILD)/__simplehttpfileserver $(BUILD)/__simplehttpfileserver_static
	ulimit -c unlimited && \
	$(BUILD)/__simplehttpfileserver -p 9999 -w -1 -d localhost:9999 -m /s /home/ugorji/Downloads

server.static: $(BUILD)/__simplehttpfileserver $(BUILD)/__simplehttpfileserver_static
	ulimit -c unlimited && \
	$(BUILD)/__simplehttpfileserver_static -p 9999 -w -1 -d localhost:9999 -m /s /home/ugorji/Downloads

server.valgrind: $(BUILD)/__simplehttpfileserver $(BUILD)/__simplehttpfileserver_static
	ulimit -c unlimited && \
	valgrind -s --track-origins=yes --leak-check=full --show-reachable=yes \
	$(BUILD)/__simplehttpfileserver -p 9999 -w -1 -d localhost:9999 -m /s /home/ugorji/Downloads

server.massif: $(BUILD)/__simplehttpfileserver $(BUILD)/__simplehttpfileserver_static
	ulimit -c unlimited && \
	valgrind -s --tool=massif \
	$(BUILD)/__simplehttpfileserver -p 9999 -w -1 -d localhost:9999 -m /s /home/ugorji/Downloads

