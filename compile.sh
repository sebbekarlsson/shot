P=$(pwd)
mkdir -p build && cd ./build && cmake .. && make -j$(nproc)
cd $P
