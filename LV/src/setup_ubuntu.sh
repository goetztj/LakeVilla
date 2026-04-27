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
cd aws-sdk-cpp && mkdir build && cd build && cmake ./../aws-sdk-cpp -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/LakeVilla/aws -DBUILD_ONLY="s3"
cmake --build . --config=Release

#build around current aws bug
#ln -s lib/libs2n.so.1 lib/libs2n.so.1.0.0

cmake --install . --config=Release


#cmake ./../src   -DCMAKE_PREFIX_PATH=/LakeVilla/aws   -DCMAKE_BUILD_TYPE=Release