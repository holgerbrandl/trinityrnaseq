#include <stdlib.h>
#include <map>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iterator>
#include <math.h>

#include "Fasta_reader.hpp"
#include "sequenceUtil.hpp"
#include "argProcessor.hpp"
#include "DeBruijnGraph.hpp"
#include "string_util.hpp"
#include "irke_common.hpp"

#ifdef _OPENMP
#include <omp.h>
#else
#define omp_get_max_threads() 1
#define omp_get_num_threads() 1
#define omp_get_thread_num() 0
#endif



using namespace std;

unsigned int IRKE_COMMON::MONITOR = 0;





// prototypes
string usage (ArgProcessor args);
int fastaToDeBruijn (int argc, char* argv[]);
DeBruijnGraph constructDeBruijnGraph (vector<string> fasta_filenames, int kmer_length, int component_val, bool sStrand, ArgProcessor args);
void createGraphPerRecord(vector<string> fasta_filenames, int kmer_length, bool sStrand, ArgProcessor args);


string usage (ArgProcessor args) {
    
    stringstream usage_info;
    
    usage_info 
        << endl << endl
        
        << "**Required" << endl
        << "  --fasta  <str>      " << ":fasta file containing reads" << endl
        << "  -K  <int>           " << ":kmer length" << endl
        << endl
        << " **Optional" << endl
        << "  -C <int>            " << ":component identifier" << endl
        << "  --SS                " << ":indicates strand-specific" << endl 
        << "  --graph_per_record " << ": write separate graph for each fasta entry." << endl
        << "  --toString          " << ": dump graph as descriptive output" << endl
        << "  --monitor <int>     " << ": verbosity level" << endl
        << "  --threads <int>     " << ": number of threads to utilize. " << endl
        << endl;
    
    return(usage_info.str());
    
}



int fastaToDeBruijn (int argc, char* argv[]) {

    try {
        
        ArgProcessor args(argc, argv);
        
        /* Check for essential options */
        if (args.isArgSet("--help") || args.isArgSet("-h")
            ||  (! (args.isArgSet("--fasta") && args.isArgSet("-K") 
                    && 
                    ( 
                     args.isArgSet("-C")
                     ||
                     args.isArgSet("--graph_per_record")
                      ) 
                    )
                 ) 
            ) {
            
            cerr  << usage(args) << endl << endl;
            
            return 1;
        }
        
        // required
        string fasta_filename = args.getStringVal("--fasta");
        int kmer_length = args.getIntVal("-K");
        
        // optional
        bool sStrand = false;
        if (args.isArgSet("--SS")) {
            sStrand = true;
        }

        if (args.isArgSet("--monitor")) {
            IRKE_COMMON::MONITOR = args.getIntVal("--monitor");
        }
        
        if (args.isArgSet("--threads")) {
            int num_threads = args.getIntVal("--threads");
            
            omp_set_num_threads(num_threads);
        }
        
        vector<string> fasta_filenames;
        if (fasta_filename.find(',') != string::npos) {
            // cerr << "Splitting filenames out: " << fasta_filename << endl;
            string_util::tokenize(fasta_filename, fasta_filenames, ",");
        }
        else {
            //cerr << "Using single fasta filename: " << fasta_filename << endl;
            fasta_filenames.push_back(fasta_filename);
        }
        

        if (args.isArgSet("--graph_per_record")) {
            
            createGraphPerRecord(fasta_filenames, kmer_length, sStrand, args);
            

        }
        else {
            
            // one big graph
            
            int component_val = args.getIntVal("-C");
            
            constructDeBruijnGraph(fasta_filenames, kmer_length, component_val, sStrand, args);
            
        }
    }
    
    catch(exception& e) {
        cerr << "error: " << e.what() << "\n";
        return 1;
    }
    
    
    
    return(0);
    
}


void createGraphPerRecord(vector<string> fasta_file_names, int kmer_length, bool sStrand, ArgProcessor args) {
    
    
    for (int i = 0; i < fasta_file_names.size(); i++) {
        
        string fasta_filename = fasta_file_names[i];
        
        if (IRKE_COMMON::MONITOR > 1)
            cerr << "Parsing file: " << fasta_filename << endl;
        
        Fasta_reader fasta_reader(fasta_filename);
        
        
        #pragma omp parallel
        while (true) {

            Fasta_entry fe;
            bool got_fe = false;

            #pragma omp critical
            if (fasta_reader.hasNext()) {
            
                fe = fasta_reader.getNext();
                got_fe = true;
            }
            
            if (! got_fe) {
                break; // no more fasta entries.
            }
            
            string accession = fe.get_accession();
            
            // get component value
            vector<string> acc_pts;
            string_util::tokenize(accession, acc_pts, "_");
            int component_id = atoi(acc_pts[1].c_str());
            
            string sequence = fe.get_sequence();
            

            DeBruijnGraph g(kmer_length);

            vector<string> seq_regions;
            string_util::tokenize(sequence, seq_regions, "X"); // inchworm bundles concatenated with 'X' delimiters by Chrysalis 
            
            for (int s = 0; s < seq_regions.size(); s++) { 
                
                string seq_region = seq_regions[s];
                
                if (contains_non_gatc(seq_region)) {
                    seq_region = replace_nonGATC_chars_with_A(seq_region);
                }
                

                if (IRKE_COMMON::MONITOR > 2) 
                    cerr << "Adding sequence to graph: " << seq_region << endl;

                g.add_sequence(seq_region);
                
                if (! sStrand) {
                    string revseq = revcomp(seq_region);
                    
                    if (IRKE_COMMON::MONITOR > 2) 
                        cerr << "Adding sequence to graph: " << revseq << endl;


                    g.add_sequence(revseq);
                    
                    

                } 
            } // end sequence region
            
            if (args.isArgSet("--toString")) {
                #pragma omp critical
                cout << g.toString();
            }
            else {
                #pragma omp critical
                cout << g.toChrysalisFormat(component_id, sStrand);
            }
            

        } // end fasta entry
        
    }  // end fasta file 
    
    return;
    
    
    

}   


DeBruijnGraph constructDeBruijnGraph (vector<string> fasta_file_names, int kmer_length, int component_val, bool sStrand, ArgProcessor args) {


    DeBruijnGraph g(kmer_length);
    
    for (int i = 0; i < fasta_file_names.size(); i++) {
        
        string fasta_filename = fasta_file_names[i];
        
        if (IRKE_COMMON::MONITOR > 1)
            cerr << "Parsing file: " << fasta_filename << endl;
        
        Fasta_reader fasta_reader(fasta_filename);
        
        
        
        while (fasta_reader.hasNext()) {
            
            Fasta_entry fe = fasta_reader.getNext();
            string sequence = fe.get_sequence();
            
            vector<string> seq_regions;
            string_util::tokenize(sequence, seq_regions, "X"); // inchworm bundles concatenated with 'X' delimiters by Chrysalis 
            
            for (int s = 0; s < seq_regions.size(); s++) { 
                
                string seq_region = seq_regions[s];
                
                if (contains_non_gatc(seq_region)) {
                    seq_region = replace_nonGATC_chars_with_A(seq_region);
                }
                

                if (IRKE_COMMON::MONITOR > 2) 
                    cerr << "Adding sequence to graph: " << seq_region << endl;

                g.add_sequence(seq_region);
                
                if (! sStrand) {
                    string revseq = revcomp(seq_region);
                    
                    if (IRKE_COMMON::MONITOR > 2) 
                        cerr << "Adding sequence to graph: " << revseq << endl;


                    g.add_sequence(revseq);
                    
                    

                }
            }
        }
    }



    if (args.isArgSet("--toString")) {
        cout << g.toString();
    }
    else {
        cout << g.toChrysalisFormat(component_val, sStrand);
    }
    
   
    return(g);
    
    
    

} 



    
int main (int argc, char* argv[]) {
    
    try {
        return(fastaToDeBruijn(argc, argv));
    }
    
    catch (string err) {
        cerr << err << endl;
    }
    
    return(1);
    
}


