// Transperancy_Messenger_Client.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include <cstdlib> 
#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <cryptopp/osrng.h>
#include <cryptopp/files.h>
#include <cryptopp/rsa.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/filters.h>
#include <cryptopp/base64.h>
#include <cryptopp/hex.h>
#include <queue>
//JSON
#include <nlohmann/json.hpp>
//#include <boost/property_tree/ptree.hpp>
//#include <boost/property_tree/json_parser.hpp>
//GUI



namespace fs = boost::filesystem;
//namespace pt = boost::property_tree;
using namespace std;
using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;
using namespace CryptoPP;
using json = nlohmann::json;

struct Globals
{
    int state;
    CryptoPP::StringSink* snk;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> *socket;
    queue<string> *que;
    boost::asio::io_context* io_context;
    mutex *m;
    std::ofstream* outputfile;
    json* j;
    string JSONfilename;
};
static Globals globals;

void tokenize(vector<string>& vec, string& s, string del)
{
    int start, end = -1 * del.size();
    do {
        start = end + del.size();
        end = s.find(del, start);
        vec.emplace_back(s.substr(start, end - start));
    } while (end != -1);
}

void tokenize(vector<string>& vec, const string& s, string del)
{
    int start, end = -1 * del.size();
    do {
        start = end + del.size();
        end = s.find(del, start);
        vec.emplace_back(s.substr(start, end - start));
    } while (end != -1);
}

std::string bytesToHexString(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    for (const auto& byte : bytes) {
        ss << std::setw(2) << static_cast<int>(byte);
    }

    return ss.str();
}

class ConstantRNG : public CryptoPP::RandomNumberGenerator
{
public:
    ConstantRNG(const std::vector<uint8_t>& constantValue) : constantValue_(constantValue) {}

    void GenerateBlock(byte* output, size_t size) override
    {
        std::fill_n(output, size, 0); // Fill the output buffer with zeroes
        std::copy_n(constantValue_.data(), std::min(size, constantValue_.size()), output); // Copy the constant value
    }

private:
    std::vector<uint8_t> constantValue_;
};

std::string encryptStringWithPublicKey(CryptoPP::RSA::PublicKey& publicKey, std::string plaintextMessage)
{
    try
    {
        // Create encryptor object
        CryptoPP::RSAES_OAEP_SHA_Encryptor encryptor(publicKey);

        /*
        std::string seedValue = "MySeedValue";
        CryptoPP::AutoSeededX917RNG<CryptoPP::AES> rng(seedValue.data(), seedValue.size());
        */

        std::vector<uint8_t> constantValue = { 0xDE, 0xAD, 0xBE, 0xEF }; // Example constant value
        ConstantRNG rng(constantValue);

        // Encrypt the message
        std::vector<uint8_t> encryptedBytes;
        CryptoPP::StringSource ss(plaintextMessage, true /*pumpAll*/,
            new CryptoPP::PK_EncryptorFilter(rng, encryptor,
                new CryptoPP::HexEncoder(new CryptoPP::VectorSink(encryptedBytes))));

        std::string hexString = bytesToHexString(encryptedBytes);
        return hexString;
    }
    catch (const Exception& ex)
    {
        std::cerr << "Crypto++ library exception: " << ex.what() << std::endl;
        return "";
    }
}


// Function to convert a hex string to bytes
std::vector<uint8_t> hexStringToBytes(const std::string& hexString) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hexString.length() / 2);

    for (std::size_t i = 0; i < hexString.length(); i += 2) {
        std::istringstream iss(hexString.substr(i, 2));
        uint8_t byte;
        iss >> std::hex >> byte;
        bytes.push_back((byte));
    }

    return bytes;
}

std::string decryptHexWithPrivateKey(CryptoPP::RSA::PrivateKey& privateKey, const std::string& hexString) {
    {
        try
        {
        std::vector<uint8_t> encryptedBytes = hexStringToBytes(hexString);
        CryptoPP::RSAES_OAEP_SHA_Decryptor decryptor(privateKey);

        std::vector<uint8_t> constantValue = { 0xDE, 0xAD, 0xBE, 0xEF }; // Example constant value
        ConstantRNG rng(constantValue);

        std::string decryptedMessage;
        CryptoPP::DecodingResult result = decryptor.Decrypt(rng, reinterpret_cast<CryptoPP::byte*>(encryptedBytes.data()), encryptedBytes.size(), reinterpret_cast<CryptoPP::byte*>(const_cast<char*>(decryptedMessage.data())));
        if (!result.isValidCoding)
        {
            throw std::runtime_error("Decryption failed");
        }
        decryptedMessage.resize(result.messageLength);
        return decryptedMessage;
        }
        catch (const Exception& ex)
        {
			std::cerr << "Crypto++ library exception: " << ex.what() << std::endl;
        }
        
        return "";
    }

    
}

void generateKeyPair(const std::string& privateKeyPath, const std::string& publicKeyPath)
{
    CryptoPP::AutoSeededRandomPool rng;

    CryptoPP::RSA::PrivateKey privateKey;
    privateKey.GenerateRandomWithKeySize(rng, 1024);

    CryptoPP::RSA::PublicKey publicKey(privateKey);

    CryptoPP::ByteQueue privateKeyBytes;
    privateKey.Save(privateKeyBytes);

    CryptoPP::ByteQueue publicKeyBytes;
    publicKey.Save(publicKeyBytes);

    CryptoPP::FileSink privateKeyFile(privateKeyPath.c_str());
    privateKeyBytes.CopyTo(privateKeyFile);

    CryptoPP::FileSink publicKeyFile(publicKeyPath.c_str());
    publicKeyBytes.CopyTo(publicKeyFile);
}

bool checkKeysExist(const std::string& privateKeyPath, const std::string& publicKeyPath)
{
    return fs::exists(privateKeyPath) && fs::exists(publicKeyPath);
}

void loadKeyPair(const std::string& privateKeyPath, const std::string& publicKeyPath,
    CryptoPP::RSA::PrivateKey& privateKey, CryptoPP::RSA::PublicKey& publicKey)
{
    CryptoPP::ByteQueue privateKeyBytes;
    CryptoPP::FileSource privateKeyFile(privateKeyPath.c_str(), true /*pumpAll*/);
    privateKeyFile.TransferTo(privateKeyBytes);
    privateKeyBytes.MessageEnd();
    privateKey.Load(privateKeyBytes);

    CryptoPP::ByteQueue publicKeyBytes;
    CryptoPP::FileSource publicKeyFile(publicKeyPath.c_str(), true /*pumpAll*/);
    publicKeyFile.TransferTo(publicKeyBytes);
    publicKeyBytes.MessageEnd();
    publicKey.Load(publicKeyBytes);
}

void do_read(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& socket, std::array<char, 1024>& read_buffer )
{
    
    read_buffer.fill(0);
    socket.async_read_some(boost::asio::buffer(read_buffer),
        [&socket,&read_buffer](const boost::system::error_code& error, size_t length) {
            if (!error) {

                std::string recieved = std::string(read_buffer.data(), length);
                std::cout << "Received: " << recieved << std::endl;

                //parses recieved by linebreaks
                std::vector<std::string> lines;
                tokenize(lines,recieved, "\n");
                
                //REFERENCE:
                //"CODE=250\nMESSAGE RECIEVED\n"+ gid_str + "\n" + to_string(UID) + ": " + mes_str + "\n"
                //"CODE=251\nGROUP ADDITION\nGID=" + to_string(gid) + "\nKEY=" + symKeys[i] +"\n"
                //"CODE=4XX\nERROR MESSAGE\n" + error_message + "\n"
                json& jsonData = *globals.j;

                if (lines.size() > 1) //Makes sure nothing is quite off
                {
                    //Checks for code
                    std::vector<std::string> code;
                    tokenize(code, lines[0], "=");
                    if (code.size() > 1)
                    {
                        if (code[0] == "CODE")
                        {
							if (code[1] == "250") //Message recieved
							{ 
                                //finds the group in the json and appends the message
                                for (auto& val : jsonData["groups"])
                                {
                                    if (val["groupid"] == stoi(lines[2]))
                                    {
                                        val["messages"].push_back(lines[3]);
                                        break;
                                    }

                                }
                                

                            }
                            if (code[1] == "251")
                            {
                                //creates a new group in the json and adds the key
                                std::vector<std::string> result;
                                tokenize(result, lines[2], "=");
                                //creates a subtree json for the group
                                json group;
                                group["gid"] = result[1];
                                result.clear();
                                tokenize(result, lines[3], "=");
                                group["key"] = result[1];
                                group["lastmesid"] = 0;
                                group["messages"] = json::array();
                                jsonData["groups"].push_back(group);

                            }
                            else if (code[1][0] == '4')
                            {

                            }
                        }
                    }

                }


                do_read(socket,read_buffer);    
            }
        }

    );

}

void do_write(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& socket, std::array<char, 1024>& write_buffer)
{
    socket.async_write_some(boost::asio::buffer(write_buffer),


        [write_buffer](const boost::system::error_code& error, size_t length) {
            if (!error) {
              std::cout<< "Successfully written: " << write_buffer.data() << endl;
            }
            else
            {
              std::cout<< "Failed to write: " << write_buffer.data() << endl;

            }

        }
        

    );
}

void draw_GUI()
{
    //system("cls");
    json& jsonData = *globals.j;
    //cout << jsonData.dump(4) << endl;

    std::cout << "\n-+=}>|-------------------------------------|<{=+-\n" << std::endl;

    std::cout << "" << std::endl;
    std::cout << "           |----------------------|" << std::endl;
    std::cout << "           |   ################   |" << std::endl;
    std::cout << "           |   ################   |" << std::endl;
    std::cout << "           |         ####         |" << std::endl;
    std::cout << "           |         ####         |" << std::endl;
    std::cout << "           |         ####         |" << std::endl;
    std::cout << "           |         ####         |" << std::endl;
    std::cout << "           |         ####         |" << std::endl;
    std::cout << "           |         ####         |" << std::endl;
    std::cout << "           |         ####         |" << std::endl;
    std::cout << "           |----------------------|" << std::endl;
    std::cout << "" << std::endl;

    std::cout << "-+=}>|-------------------------------------|<{=+-" << std::endl;

    //Iterates over all groups and prints the group id, and on lower lines their meseges
    for (auto& group : jsonData["groups"])
    {
		std::cout << "\n|----------------|" << std::endl;
		std::cout << "  Group ID:" << group["groupid"] << std::endl;
        std::cout << "|----------------|" << std::endl;

        for (auto& message : group["messages"])
        {
			std::cout << "    " << message.get<string>() << std::endl;
		}
	}

    std::cout << "\n-+=}>|-------------------------------------|<{=+-\n" << std::endl;

    switch (globals.state)
    {

        case(0):
    
            cout << "What operation would you like to do?" << endl;
            cout << "1. Create a group" << endl;
            cout << "2. Post to a group" << endl;
            break;
    
        case(1):
    
            cout <<"Write the uids of the users you wish to add to this new group. (Comma delimitered):" << endl;
            break;
        case(2):
    
            cout << "Write the group id you wish to post to, followed by an ampersand and the messege you wish to write:" << endl;
    		break;
        case(3):
	
            cout << "Invalid input, try again.\n" << endl;
            cout << "What operation would you like to do?" << endl;
            cout << "1. Create a group" << endl;
            cout << "2. Post to a group" << endl;
            break;
        case(5):
            cout << "Manual override initiated:" << endl;
            break;
    }
    

}

void console_adaptor(queue<string> &que,mutex &m)
{
    while (1)
    {

        draw_GUI();

        // Read input from console
        std::string input;
        std::getline(std::cin, input);

        if (globals.state == 3)
        {
            globals.state = 0;
        }

        if (globals.state == 0)
        {
            if (input[0] == '1')
            {
                globals.state = 1;
            }
            else if (input[0] == '2')
            {
                globals.state = 2;
			}
            else if (input[0] == '5')
            {
                globals.state = 5;
            }
            else
            {
                globals.state = 3;
            }
            draw_GUI();
        }
        if (globals.state == 1)
        {
            string input;
            std::getline(std::cin, input);
            //tokenises the input by comma
            vector<string> result;
            tokenize(result,input, ",");
            if (result.size() < 1)
            {
                globals.state = 3;
            }
            else
            {
                //checks that all values in result are numbers
                for (int i = 0; i < result.size(); i++)
                {
                    stringstream ss(result[i]);
                    int num;

                    ss >> num;

                    if (ss.fail()) {
    					globals.state = 3;
                    }

				}
                if (globals.state == 1)
                {

                    //REFERENCE
                    // "^CREATEGR UIDS=\\[(?:\\d{1,10})(?:,\\d{1,10}){0,19}\\] KEYS=\\[(?:[0-9A-F]+)(?:,[0-9A-F]+)*\\]")
                    //Create a CREATEGR message
                    json& jsonData = *globals.j;
                    stringstream ss;
                    ss << "CREATEGR UIDS=[" << jsonData["uid"]<<",";
                    ss << input << "] KEYS=[0";
                    for (int j = 0; j < result.size(); j++)
                    {
                        ss << ",0";
                    }
                    ss << "]";

                    string* message = new string();
                    *message = ss.str();
                    cout << *message << endl;
                    //send the message
                    (*globals.m).lock();
                    (*globals.socket).async_write_some(boost::asio::buffer(*message),
                        [&](const boost::system::error_code& error, size_t length) {
                            delete message;
						}
                    					);
                    (*globals.m).unlock();
                    globals.state = 0;
                }
            }


        }
        if (globals.state == 2)
        {
            std::string input;
            std::getline(std::cin, input);
            //tokenises the input by ampersand
            vector<string> result;
            tokenize(result, input, "&");
            if (result.size() != 2)
            {
                globals.state = 3;
            }
            else
            {
                //REFERENCE
                //^POST GID=\\d{1,10} KEYID=\\d{1,5} MES=[0-9A-F]+\\]
                //Create a POST message

                json& jsonData = *globals.j;
                stringstream ss;
                ss << "POST GID=";
                ss << result[0] << " KEYID=0 MES=";
                ss << result[1] << "]";
                string* message = new string();
                *message = ss.str();
                cout << *message << endl;
                //send the message
				(*globals.m).lock();
				(*globals.socket).async_write_some(boost::asio::buffer(*message),
                    [&](const boost::system::error_code& error, size_t length) {
						delete message;
					});
				(*globals.m).unlock();
                globals.state = 0;
            }
		}
        if (globals.state == 5)
        {
            //sends the input to the server
            std::getline(std::cin, input);
            string* message = new string();
            *message = input;
            //send the message
			(*globals.m).lock();
            (*globals.socket).async_write_some(boost::asio::buffer(*message),
                [&](const boost::system::error_code& error, size_t length) {
                    delete message;
                });
            (*globals.m).unlock();
            globals.state = 0;
        }

        //m.lock();
        //que.push(input);
        //m.unlock();


    }


}

void Clear_Queue(boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& socket,queue<string>& que, boost::asio::io_context& io_context,mutex &m)
{

    m.lock();

    while(!que.empty())
    {
        std::array<char, 1024> myArray{}; // Destination array
        string myString = que.front() ;
        que.pop();
        std::copy(myString.begin(), myString.end(), myArray.begin());
        do_write(socket,myArray);
    }
    m.unlock();

    io_context.post([&socket, &que, &io_context, &m]() {Clear_Queue(socket,que,io_context,m); });

}

void publicKeyToHex(const CryptoPP::RSA::PublicKey& publicKey,std::string& encodedPublicKey)
{

    globals.snk = new CryptoPP::StringSink(encodedPublicKey);
    CryptoPP::HexEncoder* encoder = new CryptoPP::HexEncoder(globals.snk);
    publicKey.Save(*encoder);
    (*encoder).MessageEnd();

    delete encoder;
    //delete sink;

    return;

}

std::string privateKeyToHex(const CryptoPP::RSA::PrivateKey& privateKey)
{
    std::string encodedPrivateKey;
    CryptoPP::StringSink* sink = new CryptoPP::StringSink(encodedPrivateKey);

    // HexEncoder provides the hex encoding functionality
    CryptoPP::HexEncoder encoder(sink);
    privateKey.Save(encoder);

    // Finalize the encoding
    encoder.MessageEnd();

    // Clean up the sink
    delete sink;

    return encodedPrivateKey;
}

void updateJSONfile()
{
    std::ofstream outputFile(globals.JSONfilename, std::ios::trunc);
    outputFile << (globals.j)->dump(4);
    outputFile.flush();
    outputFile.close();
}

void atexit_handler()
{

    std::ofstream outputFile(globals.JSONfilename, std::ios::trunc);
    outputFile << (*globals.j).dump(4);
    outputFile.flush();
    outputFile.close();
    delete globals.j;
    std::cout << "File saved.\n";
}

const int a = atexit(atexit_handler);


int main() {
    //Key pair protocol

    const std::string sslFolder = "SSL";
    const std::string privateKeyPath = sslFolder + "/private.key";
    const std::string publicKeyPath = sslFolder + "/public.key";

    if (!fs::exists(sslFolder))
    {
        fs::create_directory(sslFolder);
        std::cout << "SSL folder created.\n";
    }
    if (checkKeysExist(privateKeyPath, publicKeyPath))
    {
        std::cout << "Keys already exist in the SSL folder.\n";
        // Load keys and proceed with further operations if needed
        CryptoPP::RSA::PrivateKey privateKey;
        CryptoPP::RSA::PublicKey publicKey;
        loadKeyPair(privateKeyPath, publicKeyPath, privateKey, publicKey);


    }
    else
    {
        generateKeyPair(privateKeyPath, publicKeyPath);
        std::cout << "Key pair generated and saved in the SSL folder.\n";
    }
    // Load keys and proceed with further operations if needed
    CryptoPP::RSA::PrivateKey privateKey;
    CryptoPP::RSA::PublicKey publicKey;
    loadKeyPair(privateKeyPath, publicKeyPath, privateKey, publicKey);

    //SSL protocol

    boost::asio::io_context io_context;
    boost::asio::ssl::context ssl_context(boost::asio::ssl::context::tlsv12_client);
    
    globals.io_context = &io_context;

    // Load the self-signed certificate into the SSL context
    ssl_context.load_verify_file("SSL/server_certificate.crt");

    // Create an SSL socket and connect to the server
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket(io_context, ssl_context);
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), 8443);
    globals.socket = &socket;
    socket.lowest_layer().connect(endpoint);
    

    // Perform the SSL handshake
    socket.handshake(boost::asio::ssl::stream_base::handshake_type::client);

    std::cout << "SSL handshake completed successfully!" << std::endl;

    queue<string> consoleQueue;
    mutex m;
    globals.m = &m;
    globals.que = &consoleQueue;

    /*
    std::array<char, 1024> myArray2;
    Clear_Queue(socket, consoleQueue, io_context, m);
    */
    /*
    std::string a = encryptStringWithPublicKey(publicKey, "AAA");
    cout << a << endl<<"\n";
    a = encryptStringWithPublicKey(publicKey, "AAA");
    cout << a << endl << "\n";
    a = encryptStringWithPublicKey(publicKey, "AAA");
    cout << a << endl << "\n";
    */
    //cout << decryptHexWithPrivateKey(privateKey, a)<< "\n" << endl;



    // Write the JSON object to the file
    globals.JSONfilename = "Message_Context.json";
    std::string& jsonFileName = globals.JSONfilename;
    
    
    globals.j = new json();
    json& jsonData = *globals.j;

    bool jsonExists = fs::exists(jsonFileName);

    
    // Check if the file exists
    if (jsonExists) {
      std::cout<< "JSON file exists" << endl;
        // File exists, load it into the JSON object
        std::ifstream file(jsonFileName);
        std::stringstream buffer;
        buffer << file.rdbuf(); // Read the contents of the file into a stringstream

        std::string fileContents = buffer.str();

        try {
            jsonData = json::parse(fileContents);
        }
        catch (const std::exception& e) {
            std::cerr << "Error parsing JSON: " << e.what() << std::endl;
            return 1;
        }

        file.close();
        // jsonData = json::parse(file);
    }
    else {
        // File doesn't exist, generate a new JSON object

        jsonData["uid"] = nullptr;
        jsonData["token"] = nullptr;
        jsonData["groups"] = json::array();
        jsonData["pubkeys"] = json::array();

        std::cout << jsonData.dump(4) << std::endl;

        updateJSONfile();
    }

    if (!jsonExists)
    {
        //Initiate signup

        //initialise a string with hex encoded public key
        std::string hexPublicKey;
        publicKeyToHex(publicKey,hexPublicKey); 
        //delete globals.snk;
        


        //Initialise a SIGNUP request string with the hex encoded public key
        std::string signupRequest = "SIGNUP " + hexPublicKey + "]";

        //Send the SIGNUP request to the server
        //boost::asio::write(socket, boost::asio::buffer(signupRequest));
        socket.write_some(boost::asio::buffer(signupRequest));

        //Read the response from the server
        std::array<char, 1024> myArray;
        
        int timeoutDuration = 10;

        // Perform the read operation
        socket.read_some(boost::asio::buffer(myArray));

        // Run the I/O service on the main thread


        std::string response = myArray.data();
        //std::cout << response << std::endl;

        //REFERENCE:
        //"CODE=200\nSIGNUP SUCCESS\nUID=" + rows[0][0] + " TOKEN=" + std::to_string(token)

        //Parse the response
        std::vector<std::string> tokens;
        tokenize(tokens, response, "\n");
        std::vector<std::string> tokens2;
        tokenize(tokens2, tokens[2], " ");
        

        //Extracts rows[0][0] from UID=rows[0][0]
        std::string uid = tokens2[0].substr(4, tokens2[0].length() - 4);

        //Extracts token from TOKEN=token
        std::string token = tokens2[1].substr(6, tokens2[1].length() - 6);



        //Update the JSON object
        jsonData["token"] = stoi(token);
        jsonData["uid"] = stoi(uid);


        updateJSONfile();

    }
    else 
    {
        //REFERENCE: "^SIGNIN UID=\\d{1,10} TOKEN=\\d+\\]"
        //Prepares SIGNIN request
        std::string signinRequest = "SIGNIN UID=" + std::to_string(jsonData["uid"].get<int>()) + " TOKEN=" + std::to_string(jsonData["token"].get<int>()) + "]";

        //Send the SIGNIN request to the server
        socket.write_some(boost::asio::buffer(signinRequest));

        //REFERENCE "CODE=200\nSIGNIN SUCCESS"
        // "CODE=401\nSIGNIN FAILURE"
        // "CODE=500\nSIGNIN FAILURE"
        //Read the response from the server
        std::array<char, 1024> myArray;
        socket.read_some(boost::asio::buffer(myArray));

        //Checks the response code
        std::string response = myArray.data();
        std::vector<string> tokens;
        tokenize(tokens, response, "\n");
        if (tokens[0] != "CODE=200")
        {
			return 1;
		}


        //REFERENCE FOR GETCONTEXT:
        //"^CONTEXT CURRENT=\\[(?:\\d+,\\d+)(?:&\\d+,\\d+)*\\]"
        
        //iterates over all groups and finds their last mesid, while constructing a request string
        stringstream ss;
        ss << "CONTEXT CURRENT=[";

        //checks that there are groups in the json file
        //if not, gets context for group 0 from mesid 0
        if (jsonData["groups"].size() == 0)
        {
            ss << "2147483647,0]";
        }
        else
        {
            for (const auto& value : jsonData["groups"])
            {

                std::string groupid = value["groupid"];
                int lastmesid = value["lastmesid"];
                
                /*
                int groupid = value["groupid"].get<int>();
                int lastmesid = value["lastmesid"].get<int>();
                */


                std::string groupidlastmesid = groupid + "," + to_string(lastmesid);

                if (value == jsonData["groups"][0])
                {
                }
                else
                {
                    ss << "&";
                }
                ss << groupidlastmesid;

            }
            ss << "]";
        }

        //Send the GETCONTEXT request to the server
        socket.write_some(boost::asio::buffer(ss.str()));
        
        //Read the response from the server
        

        
        std::array<char, 1024> myArray2;
        char DEFAULT_VALUE = '\0';
        for (int i = 0; i < myArray2.size(); ++i) {
            myArray2[i] = DEFAULT_VALUE;
        }

        socket.read_some(boost::asio::buffer(myArray2));
        std::string response2 = myArray2.data();
        //REFERENCE: CODE=200\n<gid>,<mes>,<mes>\n<gid>,<mes>,<mes>&<keyid>,<key_body>\n<keyid>,<key_body>
        //REFERENCE: CODE=401\nGETCONTEXT FAILURE

        //Tokenises the repsponse by '&' and then by linebreaks
        std::vector<string> tokens2;
        tokenize(tokens2, response2, "&");
        std::vector<string> tokens3;
        tokenize(tokens3, tokens2[0], "\n");
        std::vector<string> tokens4;
        if (tokens2.size() > 1)
        {
			tokenize(tokens4, tokens2[1], "\n");
		}

        //Checks the response code
        if (tokens3[0] != "CODE=200")
        {
          std::cout<< "Getcontext failed" << endl;
          return -1;
        }
        else
        {
            //For each ine in the response, tokenise by commas, and check if a group fitting it exists.
            //If not, create a new group and add it to the json file, then append the messages to the group.
            //Otherwise, append the messages to the group.
            
            std::vector<string> tokens6;

            //generates a group for each recieved key and initialises default values
            if (tokens2.size() > 1)
            {
                for (auto& key : tokens4)
                {
                    if (key == "")
                    {
                        continue;
                    }

					tokens6.clear();
					tokenize(tokens6, key, ",");
                    json newGroup;
                    //decrypts the aes key in tokens6[1] using the private key
                    //newGroup["key"] = tokens6[1];
                    //delete globals.snk;
                    //string decryptedKey = decryptHexWithPrivateKey(privateKey, tokens6[1]);
                    //cout << "decrypted key: " << decryptedKey << endl;
                    //cout << jsonData.dump(4) << endl;
                    //newGroup["key"] = decryptedKey;

                    newGroup["key"] = tokens6[1];
                    newGroup["groupid"] = tokens6[0];
                    newGroup["lastmesid"] = 0;
                    newGroup["messages"] = json::array();
                    jsonData["groups"].push_back(newGroup);
                    cout << jsonData.dump(4) << endl;
   
				}
            }

            for (int i=1;i< tokens3.size();i++)
            {
                string& group = tokens3[i];
                if (group == "")
                {
                    break;
                }
                tokens6.clear();
				tokenize(tokens6, group, ",");

                for (auto& value : jsonData["groups"])
                {
                    if (value["groupid"] == tokens6[0])
                    {
						value["lastmesid"] = value["lastmesid"]+ tokens6.size()-1 ;
                        for (int i = 1; i < tokens6.size(); i++)
                        {
							value["messages"].push_back(tokens6[i]);
						}
						break;
					}
				}
			}


            //Writes the json data to the file
            updateJSONfile();

        }
    }
    
    
    
    std::array<char, 1024> myArray2;
    do_read(socket,myArray2);
    std::thread t2([&m, &consoleQueue]() {console_adaptor(consoleQueue, m); });
    io_context.run();
    //std::thread t3(io_context_thread);
    
    //frame.Show(true);

    //std::ofstream outputFile(jsonFileName);
    //globals.outputfile = &outputFile;
    
    return 0;
}



//On exit closes outputfile and saves json data to file.
//On exit closes socket and io_context.


// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

