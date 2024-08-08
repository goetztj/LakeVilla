#update apt
sudo apt update

# install dependencies via apt
sudo apt -y install build-essential libcurl-dev ninja-build cmake ca-certificates lsb-release wget libjsoncpp-dev

# install the newest release of Apache Arrow
wget https://apache.jfrog.io/artifactory/arrow/$(lsb_release --id --short | tr 'A-Z' 'a-z')/apache-arrow-apt-source-latest-$(lsb_release --codename --short).deb
sudo apt -y install ./apache-arrow-apt-source-latest-$(lsb_release --codename --short).deb
sudo apt update
sudo apt -y install libparquet-dev 

# clone and compile the AWS DevKit for C++
rm -rf aws-sdk-cpp
rm -rf aws
git clone --recurse-submodules https://github.com/aws/aws-sdk-cpp.git
mkdir aws && cd aws && cmake ./../aws-sdk-cpp -DCMAKE_INSTALL_PREFIX=$PWD -DBUILD_ONLY="s3" -DBUILD_SHARED_LIBS=OFF -DENABLE_UNITY_BUILD=ON -DCMAKE_BUILD_TYPE=Release
cmake --build .
cmake --install .

sudo apt install libjsoncpp-dev

git clone https://github.com/basicthinker/YCSB-C.git
git clone https://github.com/Tencent/rapidjson.git