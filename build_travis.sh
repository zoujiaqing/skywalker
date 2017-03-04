CXX=g++
CXXFLAGS="-std=c++11 -pthread -I/usr/local/include -I/usr/include"
CXXOUTPUT="/tmp/skywalker_build_detect_platform-cxx.$$"

# Test whether LevelDB library is installed
$CXX $CXXFLAGS -x c++ - -o $CXXOUTPUT 2>/dev/null  <<EOF
    #include <leveldb/db.h>
    int main() {}
EOF
  if [ "$?" != 0 ]; then
    wget https://github.com/google/leveldb/archive/v.1.20.tar.gz
    tar zxvf v1.20.tar.gz
    cd leveldb-1.20
    make
    sudo cp -rf out-shared/libleveldb.* /usr/lib
    sudo cp -rf include/leveldb /usr/include
    rm -rf leveldb-1.20
    rm -rf v1.20.tar.gz
  fi

$CXX $CXXFLAGS -x c++ -o $CXXOUTPUT 2>/dev/null  <<EOF
    #include <voyager/core/tcp_server.h>
    int main() { }
EOF
  if [ "$?" != 0 ]; then
    wget https://github.com/QiumingLu/archive/v.1.5.tar.gz
    tar zxvf v1.5.tar.gz
    cd voyager-1.5
    ./build.sh
    cd build/release
    sudo make install
    rm -rf voyager-1.5
    rm -rf v1.5
  fi