# C++ Version of [papyrus](https://github.com/PurpurMC/papyrus)

## Installation

```sh
git clone https://github.com/0x7d8/papyrus-cpp.git --recursive
cd papyrus-cpp

# make sure you have cmake and build-essential installed
sudo apt install cmake build-essential

# make sure you have all development libraries installed
sudo apt install libssl-dev zlib1g-dev libsqlite3-dev libuv1-dev

# fully install uWebSockets & uSockets
cd uWebSockets
make
sudo make install
cd uSockets
make

# copy uSockets library
sudo cp src/*.h /usr/local/include/
sudo cp *.a /usr/local/lib/

cd ../..

# build the project
cmake .
make

# output should be at ./Papyrus
```
