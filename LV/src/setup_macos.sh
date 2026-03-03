#update apt
brew update

# install dependencies via homebrew
brew install curl cmake make apache-arrow

# clone and compile the AWS DevKit for C++
rm -rf aws-sdk-cpp
rm -rf aws
git clone --recurse-submodules https://github.com/aws/aws-sdk-cpp.git
mkdir aws && cd aws && cmake ./../aws-sdk-cpp -DCMAKE_INSTALL_PREFIX=$PWD -DBUILD_ONLY="s3"
cmake --build .
cmake --install .
