/* 
 * File:   mix_file.cpp
 * Author: ivosh-l
 * 
 * Created on 29. prosinec 2011, 11:32
 */

#include "mix_file.h"
#include <iostream>
#include "Ccrc.h"
#include <iomanip>
#include <algorithm>
#include <cctype>  
#include "CBlowfish.h"
#include "mix_dexoder.h"

using namespace std;

void t_mix_header_copy(t_mix_header* header, char * data) {
    memcpy((char *) &(header->c_files), data, 2);
    data += 2;
    memcpy((char *) &(header->size), data, 4);
}

MixFile::MixFile() {
    this->dataoffset = 0;
    globaldb = new MixData("global mix database.dat");
}

MixFile::MixFile(const MixFile& orig) {
}

MixFile::~MixFile() {
    fh.close();
}

unsigned int MixFile::get_id(t_game game, string name) {
    transform(name.begin(), name.end(), name.begin(),
            (int(*)(int)) std::toupper); // convert to uppercase
    if (game != game_ts) { // for TD and RA
        int i = 0;
        unsigned int id = 0;
        int l = name.length(); // length of the filename
        while (i < l) {
            unsigned int a = 0;
            for (int j = 0; j < 4; j++) {
                a >>= 8;
                if (i < l)
                    a += static_cast<unsigned int> (name[i]) << 24;
                i++;
            }
            id = (id << 1 | id >> 31) + a;
        }
        return id;
    } else { // for TS
        const int l = name.length();
        int a = l >> 2;
        if (l & 3) {
            name += static_cast<char> (l - (a << 2));
            int i = 3 - (l & 3);
            while (i--)
                name += name[a << 2];
        }
        Ccrc crc; // use a normal CRC function
        crc.init();
        crc.do_block(name.c_str(), name.length());
        return crc.get_crc();
        return 0;
    }
}

bool MixFile::open(const std::string path) {
    fh.open(path.c_str(), ios::binary);
    if (fh.rdstate() & ifstream::failbit) {
        cout << "Unable to read file!" << endl;
        return false;
    }
    fh.read((char*) &mix_head, sizeof (mix_head));
    dataoffset = 6;
    if (!mix_head.c_files) {
        dataoffset += 4;
        m_has_checksum = mix_head.flags & mix_checksum;
        m_is_encrypted = mix_head.flags & mix_encrypted;
        //cout << "Checksum: " << m_has_checksum << " Encrypted: " << m_is_encrypted << " " << sizeof (mix_head) << endl;
        if (m_is_encrypted) {
            //cout << "It's encrypted." << endl;
            /* read key_source */
            fh.seekg(4);
            fh.read(key_source, 80);
            get_blowfish_key((byte *) key_source, (byte *) key);
            Cblowfish blfish;
            byte enc_header[8];
            blfish.set_key((byte *) key, 56);
            /* read encrypted header */
            fh.seekg(84);
            fh.read((char*) enc_header, 8);
            blfish.decipher((void *) enc_header, (void *) enc_header, 8);
            t_mix_header_copy(&mix_head, (char *) enc_header);
            memcpy(decrypt_buffer, (char*) (&enc_header) + 6, 2);
            decrypt_size = 2;
            //cout << "Encrypted number of files: " << dec << mix_head.c_files << endl;
            //cout << "Encrypted size of all files: " << dec << mix_head.size << endl;


            get_files();
            // to be done
        } else {
            fh.seekg(4);
            fh.read((char*) &mix_head, 6);
            get_files();
        }

    }
    //cout << "Number of Files: " << mix_head.c_files << endl;
    //cout << mix_head.size << endl;
    return true;
}

void MixFile::get_files() {
    int i;
    unsigned int mixdb_offset = 0, mixdb_size = 0;
    t_mix_index_entry fheader;
    //cout << setw(16) << setfill(' ') << " ID" << " | OFFSET \t| SIZE" << endl;

    if(!fh.is_open())
        return;
    
    if (m_is_encrypted) {
        dataoffset += 80;
        readIndex(&mixdb_offset, &mixdb_size);
    } else {
        for (i = 0; i < mix_head.c_files; i++) {
            fh.read((char*) &fheader, sizeof (fheader));
            dataoffset += sizeof (fheader);
            //cout << setw(16) << setfill(' ') << hex << fheader.id << " | " << dec << fheader.offset << "\t\t| " << fheader.size << endl;
            files.push_back(fheader);

            if (fheader.id == 0x366e051f) { // local mix database.dat
                mixdb_offset = fheader.offset;
                mixdb_size = fheader.size;
            }
        }
    }

    if (mixdb_size) {
        mixdb = new MixData(&fh, mixdb_offset + dataoffset, mixdb_size);
        has_local_mixdb = true;
    } else
        has_local_mixdb = false;




    //fh.read(ecache, 256);
    //cout << "ECACHE37.MIX: " << hex << get_id(game_ts, "ECACHE37.MIX") << endl;
}

bool MixFile::checkFileName(string fname) {
    transform(fname.begin(), fname.end(), fname.begin(),
            (int(*)(int)) std::toupper);
    unsigned int fileID = get_id(game_ts, fname);
    for (int i = 0; i < files.size(); i++) {
        if (files[i].id == fileID)
            return true;
    }
    return false;
}

void MixFile::readIndex(unsigned int * mixdb_offset, unsigned int * mixdb_size) {
    int indexSize;
    int blockCnt;

    indexSize = mix_head.c_files * 12;
    blockCnt = (indexSize - decrypt_size) / 8;
    byte encBuff[8];
    Cblowfish blfish;
    char * encIndex;
    t_mix_index_entry fheader;

    if ((indexSize - decrypt_size) % 8)
        blockCnt++;

    encIndex = new char[blockCnt * 8 + decrypt_size];
    memcpy(encIndex, decrypt_buffer, decrypt_size);
    //fh.read(encIndex + decrypt_size, blockCnt * 8);

    blfish.set_key((byte *) key, 56);

    for (int i = 0; i < blockCnt; i++) {
        fh.read((char*) &encBuff, 8);
        blfish.decipher((void *) &encBuff, (void *) &encBuff, 8);
        memcpy(encIndex + decrypt_size + 8 * i, (char *) &encBuff, 8);
    }
    /*
        for (int i = 0; i < (blockCnt * 8 + decrypt_size); i++) {
            cout << hex << " " << setfill('0') << setw(2) << (unsigned short int) ((unsigned char *) encIndex)[i];
            if (!((i + 1) % 12))
                cout << endl;
            else if (!((i + 1) % 4))
                cout << " |";
        }
        cout << endl;
     */
    dataoffset += blockCnt * 8 + decrypt_size;

    for (int i = 0; i < mix_head.c_files; i++) {
        memcpy((char *) &fheader, encIndex + i * 12, 12);
        //cout << setw(16) << setfill(' ') << hex << fheader.id << " | " << dec << fheader.offset << "\t\t| " << fheader.size << endl;
        files.push_back(fheader);

        if (fheader.id == 0x366e051f) { // local mix database.dat
            *mixdb_offset = fheader.offset;
            *mixdb_size = fheader.size;
        }

    }

    //cout << "file offset: " << dec << dataoffset << endl;
}

bool MixFile::extractFile(unsigned int fileID, std::string outPath) {
    ofstream oFile;
    unsigned int f_offset = 0, f_size = 0;
    char * buffer;

    // find file index entry
    for (int i = 0; i < files.size(); i++) {
        if (files[i].id == fileID) {
            f_offset = files[i].offset;
            f_size = files[i].size;
        }
    }
    if (!f_offset)
        return false;

    buffer = new char[f_size];
    fh.seekg(dataoffset + f_offset);
    fh.read(buffer, f_size);

    oFile.open(outPath.c_str(), ios_base::binary);
    oFile.write(buffer, f_size);

    oFile.close();
    delete[] buffer;

    return true;
}

bool MixFile::extractFile(std::string fileName, std::string outPath) {
    return extractFile(get_id(game_ts, fileName), outPath);
}

void MixFile::printFileList(int flags = 1) {
    int i;

    vector<string> filenamesdb;
    vector<string> filenamesdb_local;


    if (flags & 1) {
        // get filenames
        filenamesdb = globaldb->getFileNames();
        if (has_local_mixdb)
            filenamesdb_local = mixdb->getFileNames();

        for (i = 0; i < mix_head.c_files; i++) {
            bool found = false;
            for (int j = 0; j < filenamesdb.size(); j++) {
                if (get_id(game_ts, filenamesdb[j]) == files[i].id) {
                    //cout << "filename found: " << filenamesdb[j] << ";" << endl;
                    filenames.push_back(filenamesdb[j]);
                    found = true;
                    break;
                }
            }
            if (has_local_mixdb && !found) {
                for (int j = 0; j < filenamesdb_local.size(); j++) {
                    if (get_id(game_ts, filenamesdb_local[j]) == files[i].id) {
                        //cout << "filename found: " << filenamesdb[j] << ";" << endl;
                        filenames.push_back(filenamesdb_local[j]);
                        found = true;
                        break;
                    }

                }
            }
            if (!found) {
                filenames.push_back("<unknown>");
            }

        }

        cout << setw(35) << setfill(' ') << "FILENAME |";
    }


    cout << setw(12) << setfill(' ') << "ID | " << setw(12) << setfill(' ') << "ADDRESS |" << setw(10) << setfill(' ') << "SIZE" << endl;

    for (int i = 0; i < files.size(); i++) {
        if (flags & 1) {
            cout << setw(33) << setfill(' ') << filenames[i] << " |";
        }
        cout << " " << setw(8) << setfill('0') << hex << files[i].id << " | " << setw(10) << setfill(' ') << dec << (files[i].offset + dataoffset) << " | " << setw(10) << setfill(' ') << files[i].size << endl;
    }
}