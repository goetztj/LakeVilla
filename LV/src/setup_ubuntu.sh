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
mkdir aws && cd aws && cmake ./.. -DCMAKE_INSTALL_PREFIX=/LakeVilla/aws -DBUILD_ONLY="s3" -DSIMPLE_INSTALL=ON
cmake --build .
cmake --install .
