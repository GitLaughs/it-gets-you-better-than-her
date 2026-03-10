docker load -i "a1-sdk-builder-latest.tar"

ping 127.0.0.1
ping 127.0.0.1
ping 127.0.0.1

mkdir data

docker run -itd --name A1_Builder -v "./data:/home/smartsens_flying_chip_a1_sdk" a1-sdk-builder