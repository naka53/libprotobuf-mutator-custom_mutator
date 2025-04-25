#include <fstream>
#include "x509_certificate.pb.h"

#include "asn1_pdu_to_der.h"
#include "x509_certificate_to_der.h"

std::string readFile(const char* path) {
    printf("Reading file at %s\n", path);
    std::ifstream file(path, std::ios::in | std::ios::binary);
    std::string str = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    printf("Read %ld bytes\n\n", str.length());
    return str;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Missing file path\n");
        return 1;
    }

    std::string protobuf = readFile(argv[1]);

    //x509_certificate::X509Certificate input;
    x509_certificate::SubjectPublicKeyInfo input;
    bool parse_ok = input.ParseFromArray(protobuf.c_str(), protobuf.length());
    if (!parse_ok) {
        printf("ParseFromArray failed!\n");
        return 1;
    }

    std::string serialized;
    input.SerializeToString(&serialized);

    std::vector<uint8_t> asn1 = x509_certificate::SubjectPublicKeyInfoToDER(input);

    std::ofstream fs("output.der", std::ios::out | std::ios::binary | std::ios::app);
    fs.write(reinterpret_cast<char *>(asn1.data()), asn1.size());
    fs.close();

    return 0;
}
