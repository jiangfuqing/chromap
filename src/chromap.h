#ifndef CHROMAP_H_
#define CHROMAP_H_

#include <memory>
#include <random>
#include <string>
#include <sys/time.h>
#include <sys/resource.h>
#include <tuple>
#include <vector>

#include "index.h"
#include "khash.h"
#include "ksort.h"
#include "output_tools.h"
#include "sequence_batch.h"

namespace chromap {
struct uint128_t {
  uint64_t first;
  uint64_t second;
};

struct StackCell {
  size_t x; // node
  int k, w; // k: level; w: 0 if left child hasn't been processed
  StackCell() {};
  StackCell(int k_, size_t x_, int w_) : x(x_), k(k_), w(w_) {};
};

KHASH_MAP_INIT_INT64(k128, uint128_t);
KHASH_MAP_INIT_INT(k32, uint32_t);

enum Direction {
  kPositive,
  kNegative,
};

#define SortMappingWithoutBarcode(m) (((((m).fragment_start_position<<16)|(m).fragment_length)<<8)|(m).mapq)
//#define SortMappingWithoutBarcode(m) (m)
template <typename MappingRecord = MappingWithoutBarcode>
class Chromap {
 public:
  // For index construction
  Chromap(int kmer_size, int window_size, int num_threads, const std::string &reference_file_path, const std::string &index_file_path) : kmer_size_(kmer_size), window_size_(window_size), num_threads_(num_threads), reference_file_path_(reference_file_path), index_file_path_(index_file_path) {
    barcode_lookup_table_ = NULL;
  }

  // For mapping
  Chromap(int error_threshold, int match_score, int mismatch_penalty, const std::vector<int> &gap_open_penalties, const std::vector<int> &gap_extension_penalties, int min_num_seeds_required_for_mapping, const std::vector<int> &max_seed_frequencies, int max_num_best_mappings, int max_insert_size, int num_threads, int min_read_length, int multi_mapping_allocation_distance, int multi_mapping_allocation_seed, int drop_repetitive_reads, bool trim_adapters, bool remove_pcr_duplicates, bool is_bulk_data, bool allocate_multi_mappings, bool only_output_unique_mappings, bool Tn5_shift, bool output_mapping_in_BED, bool output_mapping_in_TagAlign, bool output_mapping_in_PAF, const std::string &reference_file_path, const std::string &index_file_path, const std::string &read_file1_path, const std::string &read_file2_path, const std::string &barcode_file_path, const std::string &mapping_output_file_path) : error_threshold_(error_threshold), match_score_(match_score), mismatch_penalty_(mismatch_penalty), gap_open_penalties_(gap_open_penalties), gap_extension_penalties_(gap_extension_penalties), min_num_seeds_required_for_mapping_(min_num_seeds_required_for_mapping), max_seed_frequencies_(max_seed_frequencies), max_num_best_mappings_(max_num_best_mappings), max_insert_size_(max_insert_size), num_threads_(num_threads), min_read_length_(min_read_length), multi_mapping_allocation_distance_(multi_mapping_allocation_distance), multi_mapping_allocation_seed_(multi_mapping_allocation_seed), drop_repetitive_reads_(drop_repetitive_reads), trim_adapters_(trim_adapters), remove_pcr_duplicates_(remove_pcr_duplicates), is_bulk_data_(is_bulk_data), allocate_multi_mappings_(allocate_multi_mappings), only_output_unique_mappings_(only_output_unique_mappings), Tn5_shift_(Tn5_shift), output_mapping_in_BED_(output_mapping_in_BED), output_mapping_in_TagAlign_(output_mapping_in_TagAlign), output_mapping_in_PAF_(output_mapping_in_PAF), reference_file_path_(reference_file_path), index_file_path_(index_file_path), read_file1_path_(read_file1_path), read_file2_path_(read_file2_path), barcode_file_path_(barcode_file_path), mapping_output_file_path_(mapping_output_file_path) {
    barcode_lookup_table_ = kh_init(k32);
  } 

  ~Chromap(){
    if (barcode_lookup_table_ != NULL) {
      kh_destroy(k32, barcode_lookup_table_);
    }
    if (read_lookup_tables_.size() > 0) {
      for (uint32_t i = 0; i < read_lookup_tables_.size(); ++i) {
        kh_destroy(k128, read_lookup_tables_[i]);
      }
    }
  }

  KRADIX_SORT_INIT(without_barcode, MappingWithoutBarcode, SortMappingWithoutBarcode, 11);
  KRADIX_SORT_INIT(with_barcode, MappingWithBarcode, SortMappingWithoutBarcode, 15);

  // For paired-end read mapping
  void MapPairedEndReads();
  uint32_t LoadPairedEndReadsWithBarcodes(SequenceBatch *read_batch1, SequenceBatch *read_batch2, SequenceBatch *barcode_batch);
  void TrimAdapterForPairedEndRead(uint32_t pair_index, SequenceBatch *read_batch1, SequenceBatch *read_batch2);
  bool PairedEndReadWithBarcodeIsDuplicate(uint32_t pair_index, const SequenceBatch &barcode_batch, const SequenceBatch &read_batch1, const SequenceBatch &read_batch2);
  void ReduceCandidatesForPairedEndReadOnOneDirection(const std::vector<uint64_t> &candidates1, const std::vector<uint64_t> &candidates2, std::vector<uint64_t> *filtered_candidates1, std::vector<uint64_t> *filtered_candidates2);
  void ReduceCandidatesForPairedEndRead(const std::vector<uint64_t> &positive_candidates1, const std::vector<uint64_t> &negative_candidates1, const std::vector<uint64_t> &positive_candidates2, const std::vector<uint64_t> &negative_candidates2, std::vector<uint64_t> *filtered_positive_candidates1, std::vector<uint64_t> *filtered_negative_candidates1, std::vector<uint64_t> *filtered_positive_candidates2, std::vector<uint64_t> *filtered_negative_candidates2);
  void GenerateBestMappingsForPairedEndReadOnOneDirection(Direction first_read_direction, uint32_t pair_index, int num_candidates1, int min_num_errors1, int num_best_mappings1, int second_min_num_errors1, int num_second_best_mappings1, const SequenceBatch &read_batch1, const std::vector<std::pair<int, uint64_t> > &mappings1, int num_candidates2, int min_num_errors2, int num_best_mappings2, int second_min_num_errors2, int num_second_best_mappings2, const SequenceBatch &read_batch2, const SequenceBatch &reference, const std::vector<std::pair<int, uint64_t> > &mappings2, std::vector<std::pair<uint32_t, uint32_t> > *best_mappings, int *min_sum_errors, int *num_best_mappings, int *second_min_sum_errors, int *num_second_best_mappings);
  void RecalibrateBestMappingsForPairedEndReadOnOneDirection(Direction first_read_direction, uint32_t pair_index, int min_sum_errors, int second_min_sum_errors, int min_num_errors1, int num_best_mappings1, int second_min_num_errors1, int num_second_best_mappings1, const SequenceBatch &read_batch1, const std::vector<std::pair<int, uint64_t> > &mappings1, int min_num_errors2, int num_best_mappings2, int second_min_num_errors2, int num_second_best_mappings2, const SequenceBatch &read_batch2, const SequenceBatch &reference, const std::vector<std::pair<int, uint64_t> > &mappings2, const std::vector<std::pair<uint32_t, uint32_t> > &edit_best_mappings, std::vector<std::pair<uint32_t, uint32_t> > *best_mappings, int *best_alignment_score, int *num_best_mappings, int *second_best_alignment_score, int *num_second_best_mappings);

  void ProcessBestMappingsForPairedEndReadOnOneDirection(Direction first_read_direction, uint32_t pair_index, uint8_t mapq, int num_candidates1, int min_num_errors1, int num_best_mappings1, int second_min_num_errors1, int num_second_best_mappings1, const SequenceBatch &read_batch1, const std::vector<std::pair<int, uint64_t> > &mappings1, int num_candidates2, int min_num_errors2, int num_best_mappings2, int second_min_num_errors2, int num_second_best_mappings2, const SequenceBatch &read_batch2, const SequenceBatch &reference, const SequenceBatch &barcode_batch, const std::vector<int> &best_mapping_indices, const std::vector<std::pair<int, uint64_t> > &mappings2, const std::vector<std::pair<uint32_t, uint32_t> > &best_mappings, int min_sum_errors, int num_best_mappings, int second_min_sum_errors, int num_second_best_mappings, int *best_mapping_index, int *num_best_mappings_reported, std::vector<std::vector<MappingRecord> > *mappings_on_diff_ref_seqs);
  void GenerateBestMappingsForPairedEndRead(uint32_t pair_index, int num_positive_candidates1, int num_negative_candidates1, int min_num_errors1, int num_best_mappings1, int second_min_num_errors1, int num_second_best_mappings1, const SequenceBatch &read_batch1, const std::vector<std::pair<int, uint64_t> > &positive_mappings1, const std::vector<std::pair<int, uint64_t> > &negative_mappings1, int num_positive_candidates2, int num_negative_candidates2, int min_num_errors2, int num_best_mappings2, int second_min_num_errors2, int num_second_best_mappings2, const SequenceBatch &read_batch2, const SequenceBatch &reference, const SequenceBatch &barcode_batch, const std::vector<std::pair<int, uint64_t> > &positive_mappings2, const std::vector<std::pair<int, uint64_t> > &negative_mappings2, std::vector<int> *best_mapping_indices, std::mt19937 *generator, std::vector<std::pair<uint32_t, uint32_t> > *F1R2_best_mappings, std::vector<std::pair<uint32_t, uint32_t> > *F2R1_best_mappings, int *min_sum_errors, int *num_best_mappings, int *second_min_sum_errors, int *num_second_best_mappings, std::vector<std::vector<MappingRecord> > *mappings_on_diff_ref_seqs);
  void EmplaceBackMappingRecord(uint32_t read_id, uint32_t barcode, uint32_t fragment_start_position, uint16_t fragment_length, uint8_t mapq, uint16_t positive_alignment_length, uint16_t negative_alignment_length, std::vector<MappingRecord> *mappings_on_diff_ref_seqs);
  void EmplaceBackMappingRecord(uint32_t read_id, const char *read1_name, const char *read2_name, uint16_t read1_length, uint16_t read2_length, uint32_t barcode, uint32_t fragment_start_position, uint16_t fragment_length, uint8_t mapq, uint16_t positive_alignment_length, uint16_t negative_alignment_length, std::vector<MappingRecord> *mappings_on_diff_ref_seqs);
  void OutputPairedEndMappingsInVector(uint8_t mapq_threshold, uint32_t num_reference_sequences, const SequenceBatch &reference, const std::vector<std::vector<MappingRecord> > &mappings);
  void OutputPairedEndMappings(uint32_t num_reference_sequences, const SequenceBatch &reference, const std::vector<std::vector<MappingRecord> > &mappings);
  void ApplyTn5ShiftOnPairedEndMapping(uint32_t num_reference_sequences, std::vector<std::vector<MappingRecord> > *mappings);

  // For single-end read mapping
  void MapSingleEndReads();
  void GenerateBestMappingsForSingleEndRead(int min_num_errors, int num_best_mappings, int second_min_num_errors, int num_second_best_mappings, const SequenceBatch &read_batch, uint32_t read_index, const SequenceBatch &reference, const SequenceBatch &barcode_batch, const std::vector<std::pair<int, uint64_t> > &positive_mappings, const std::vector<std::pair<int, uint64_t> > &negative_mappings, std::vector<std::vector<MappingRecord> > *mappings_on_diff_ref_seqs);
  void ProcessBestMappingsForSingleEndRead(Direction mapping_direction, uint8_t mapq, int min_num_errors, int num_best_mappings, int second_min_num_errors, int num_second_best_mappings, const SequenceBatch &read_batch, uint32_t read_index, const SequenceBatch &reference, const SequenceBatch &barcode_batch, const std::vector<int> &best_mapping_indices, const std::vector<std::pair<int, uint64_t> > &mappings, int *best_mapping_index, int *num_best_mappings_reported, std::vector<std::vector<MappingRecord> > *mappings_on_diff_ref_seqs);
  void EmplaceBackMappingRecord(uint32_t read_id, uint32_t barcode, uint32_t fragment_start_position, uint16_t fragment_length, uint8_t mapq, std::vector<MappingRecord> *mappings_on_diff_ref_seqs);
  void EmplaceBackMappingRecord(uint32_t read_id, const char* read_name, uint16_t read_length, uint32_t barcode, uint32_t fragment_start_position, uint16_t fragment_length, uint8_t mapq, std::vector<MappingRecord> *mappings_on_diff_ref_seqs);
  void ApplyTn5ShiftOnSingleEndMapping(uint32_t num_reference_sequences, std::vector<std::vector<MappingRecord> > *mappings);
  uint32_t LoadSingleEndReadsWithBarcodes(SequenceBatch *read_batch, SequenceBatch *barcode_batch);

  // Supportive functions
  void ConstructIndex();
  int BandedAlignPatternToText(const char *pattern, const char *text, const int read_length, int *mapping_end_location);
  void BandedTraceback(int min_num_errors, const char *pattern, const char *text, const int read_length, int *mapping_start_position);
  void VerifyCandidatesOnOneDirection(Direction candidate_direction, const SequenceBatch &read_batch, uint32_t read_index, const SequenceBatch &reference, const std::vector<uint64_t> &candidates, std::vector<std::pair<int, uint64_t> > *mappings, int *min_num_errors, int *num_best_mappings, int *second_min_num_errors, int *num_second_best_mappings);
  void VerifyCandidates(const SequenceBatch &read_batch, uint32_t read_index, const SequenceBatch &reference, const std::vector<uint64_t> &positive_candidates, const std::vector<uint64_t> &negative_candidates, std::vector<std::pair<int, uint64_t> > *positive_mappings, std::vector<std::pair<int, uint64_t> > *negative_mappings, int *min_num_errors, int *num_best_mappings, int *second_min_num_errors, int *num_second_best_mappings);
  void AllocateMultiMappings(uint32_t num_reference_sequences);
  void RemovePCRDuplicate(uint32_t num_reference_sequences);
  void MoveMappingsInBuffersToMappingContainer(uint32_t num_reference_sequences, std::vector<std::vector<std::vector<MappingRecord> > > *mappings_on_diff_ref_seqs_for_diff_threads_for_saving);
  void OutputMappingStatistics();
  void OutputMappingStatistics(uint32_t num_reference_sequences, const std::vector<std::vector<MappingRecord> > &uni_mappings, const std::vector<std::vector<MappingRecord> > &multi_mappings);
  uint8_t GetMAPQ(int num_positive_candidates, int num_negative_candidates, uint16_t alignment_length, int min_num_errors, int num_best_mappings, int second_min_num_errors, int num_second_best_mappings);
  inline static double GetRealTime() {
    struct timeval tp;
    struct timezone tzp;
    gettimeofday(&tp, &tzp);
    return tp.tv_sec + tp.tv_usec * 1e-6;
  }
  inline static double GetCPUTime() {
    struct rusage r;
    getrusage(RUSAGE_SELF, &r);
    return r.ru_utime.tv_sec + r.ru_stime.tv_sec + 1e-6 * (r.ru_utime.tv_usec + r.ru_stime.tv_usec);
  }
  inline static void ExitWithMessage(const std::string &message) {
    std::cerr << message << std::endl;
    exit(-1);
  }

  void SortOutputMappings(uint32_t num_reference_sequences, std::vector<std::vector<MappingRecord> > *mappings);
  void BuildAugmentedTree(uint32_t ref_id);
  uint32_t GetNumOverlappedMappings(uint32_t ref_id, const MappingRecord &mapping);
 private:
  // Parameters
  int kmer_size_;
  int window_size_;
  int error_threshold_;
  int match_score_;
  int mismatch_penalty_;
  std::vector<int> gap_open_penalties_;
  std::vector<int> gap_extension_penalties_;
  int min_num_seeds_required_for_mapping_;
  std::vector<int> max_seed_frequencies_;
  int max_num_best_mappings_; // Read with # best mappings greater than it will have this number of best mappings reported.
  int max_insert_size_;
  int num_threads_;
  int min_read_length_;
  int multi_mapping_allocation_distance_;
  int multi_mapping_allocation_seed_;
  int drop_repetitive_reads_; // Read with more than this number of mappings will be dropped.
  bool trim_adapters_;
  bool remove_pcr_duplicates_;
  bool is_bulk_data_;
  bool allocate_multi_mappings_;
  bool only_output_unique_mappings_;
  bool Tn5_shift_;
  bool output_mapping_in_BED_;
  bool output_mapping_in_TagAlign_;
  bool output_mapping_in_PAF_;
  uint32_t read_batch_size_ = 1000000; // default batch size, # reads for single-end reads, # read pairs for paired-end reads
  std::string reference_file_path_;
  std::string index_file_path_;
  std::string read_file1_path_;
  std::string read_file2_path_;
  std::string barcode_file_path_;
  std::string mapping_output_file_path_;
  FILE *mapping_output_file_;
  // For identical read dedupe
  int allocated_barcode_lookup_table_size_ = (1 << 10);
  khash_t(k32)* barcode_lookup_table_;
  std::vector<khash_t(k128)* > read_lookup_tables_;
  // For mapping
  std::vector<std::vector<MappingRecord> > mappings_on_diff_ref_seqs_;
  std::vector<std::vector<MappingRecord> > deduped_mappings_on_diff_ref_seqs_;
  std::vector<std::pair<uint32_t, MappingRecord> > multi_mappings_;
  std::vector<std::vector<MappingRecord> > allocated_mappings_on_diff_ref_seqs_;
  std::vector<std::vector<uint32_t> > tree_extras_on_diff_ref_seqs_;
  std::vector<std::pair<int, uint32_t> > tree_info_on_diff_ref_seqs_; // (max_level, # nodes)
  std::unique_ptr<OutputTools<MappingRecord> > output_tools;
  // For mapping stats
  uint64_t num_candidates_ = 0;
  uint64_t num_mappings_ = 0;
  uint64_t num_mapped_reads_ = 0;
  uint64_t num_uniquely_mapped_reads_ = 0;
  uint64_t num_reads_ = 0;
  uint64_t num_duplicated_reads_ = 0; // # identical reads
};
} // namespace chromap

#endif // CHROMAP_H_