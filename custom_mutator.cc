#include "custom_mutator.h"
#include "x509_certificate_to_der.h"

#include "afl-fuzz.h"

#define DEBUG

// Enabling crossovers is slower (at least 50%) but can increase coverage, and
// is implemented by https://github.com/P1umer/AFLplusplus-protobuf-mutator
//#define USE_CROSSOVERS

void debug_printf(const char *format, ...) {
#ifdef DEBUG
    va_list args;
    va_start(args, format);
    vprintf(format, args);
#endif
}

#define BUF_VAR(type, name) \
    type * name##_buf;      \
    size_t name##_size;

#define BUF_PARAM(struct, name) \
    (void **)&struct->name##_buf, &struct->name##_size

typedef struct custom_mutator_ {
    afl_state_t *afl;
    unsigned int seed;
    size_t max_size = -1;
    ASN1Mutator *mutator;

    BUF_VAR(uint8_t, protobuf_out);
    BUF_VAR(uint8_t, asn1_out);
} custom_mutator_t;

#define INITIAL_GROWTH_SIZE (64)

static inline void *maybe_grow(void **buf, size_t *size, size_t size_needed) {

    /* No need to realloc */
    if (likely(size_needed && *size >= size_needed)) return *buf;

    /* No initial size was set */
    if (size_needed < INITIAL_GROWTH_SIZE) size_needed = INITIAL_GROWTH_SIZE;

    /* grow exponentially */
    size_t next_size = next_pow2(size_needed);

    /* handle overflow */
    if (!next_size) { next_size = size_needed; }

    /* alloc */
    *buf = realloc(*buf, next_size);
    *size = *buf ? next_size : 0;

    return *buf;
}

/**
 * Protobuf helpers
 */
#ifdef USE_CROSSOVERS
size_t GetRandom(unsigned int s) {
    // Inspired by https://github.com/P1umer/AFLplusplus-protobuf-mutator/blob/main/src/afl_mutator.cc#L27
    static std::default_random_engine generator(s);
    std::uniform_int_distribution<size_t> uniform_dist(1, 10);
    return uniform_dist(generator);
}

extern "C" size_t should_crossover(custom_mutator_t *data,
                                   uint8_t *buf, size_t buf_size,
                                   uint8_t *add_buf, size_t add_buf_size) {
    // According to the AFL++ documentation, add_buf may be NULL
    if (unlikely(NULL == add_buf || 0 == add_buf_size)) {
        return false;
    }

    // Inspired by https://github.com/P1umer/AFLplusplus-protobuf-mutator/blob/main/src/afl_mutator.cc#L48
    // Probability of crossover: 0.6
    // Probability of mutation: 0.4
    std::string protobuf;
    switch (GetRandom(data->seed)){
        case 1:
        case 2:
        case 3:
        case 4: {
            return false;
        }
        default:{
            return true;
        }
    }
}
#endif

void mutate(ASN1Mutator *mutator, x509_certificate::SubjectPublicKeyInfo *input,
            std::string *output, size_t max_size) {
    // Mutate the data using the custom mutator
    // Note: the documentation of Mutate states that there is no guarantee that
    // the real size will be strictly smaller than the max_size_hint. They
    // advise to repeat the mutation if the result is too large
    // Just in case, we escape after 10 times and return a large result
    int mutations = 0;
    do {
        mutations++;
        debug_printf("[mutator] [afl_custom_fuzz] Mutating input with max size of %d (try %d / 10)\n", max_size, mutations);
        mutator->Mutate(input, max_size);
        *output = input->SerializeAsString();
    } while (unlikely(output->length() > max_size) && likely(mutations < 10));
}

#ifdef USE_CROSSOVERS
void crossover(ASN1Mutator *mutator,
               x509_certificate::SubjectPublicKeyInfo *input1, x509_certificate::SubjectPublicKeyInfo *input2,
               std::string *output, size_t max_size) {
    // Perform a crossover between two messages
    debug_printf("[mutator] [afl_custom_fuzz] Performing crossover with max size of %d\n", max_size);
    mutator->CrossOver(*input1, input2, max_size);
    *output = input1->SerializeAsString();
}
#endif

/**
 * AFL++ API implementation
 */

/**
 * Initialize this custom mutator
 *
 * @param[in] afl a pointer to the internal state object. Can be ignored for
 * now.
 * @param[in] seed A seed for this mutator - the same seed should always mutate
 * in the same way.
 * @return Pointer to the data object this custom mutator instance should use.
 *         There may be multiple instances of this mutator in one afl-fuzz run!
 *         Return NULL on error.
 */
extern "C" custom_mutator_t *afl_custom_init(afl_state_t *afl, unsigned int seed) {
    debug_printf("[mutator] [afl_custom_init] Init ASN1Mutator with seed: %d\n", seed);

    custom_mutator_t *data = (custom_mutator_t *)calloc(1, sizeof(custom_mutator_t));
    if (!data) {
        perror("[mutator] [afl_custom_init] custom_mutator alloc failed");
        return NULL;
    }

    data->afl = afl;
    data->seed = seed;
    data->mutator = new ASN1Mutator();
    data->mutator->Seed(seed);
    srand(seed);
    return data;
}

/**
 * Perform custom mutations on a given input
 *
 * @param[in] data pointer returned in afl_custom_init for this fuzz case
 * @param[in] buf Pointer to input data to be mutated
 * @param[in] buf_size Size of input data
 * @param[out] out_buf the buffer we will work on. we can reuse *buf. NULL on
 * error.
 * @param[in] add_buf Buffer containing the additional test case
 * @param[in] add_buf_size Size of the additional test case
 * @param[in] max_size Maximum size of the mutated output. The mutation must not
 *     produce data larger than max_size.
 * @return Size of the mutated output.
 */
extern "C" size_t afl_custom_fuzz(custom_mutator_t *data,
                                  uint8_t *buf, size_t buf_size,
                                  uint8_t **out_buf,
                                  uint8_t *add_buf, size_t add_buf_size,
                                  size_t max_size) {
    debug_printf("[mutator] [afl_custom_fuzz] Got buf %p of size %ld and add_buf %p of size %ld\n", buf, buf_size, add_buf, add_buf_size);

    // Create a certificate object from the protobuf data
    x509_certificate::SubjectPublicKeyInfo input;
    debug_printf("[mutator] [afl_custom_fuzz] Converting raw protobuf to SubjectPublicKeyInfo\n");
    if (unlikely(!input.ParseFromArray(buf, buf_size))) {
        printf("[mutator] [afl_custom_fuzz] /!\\ ParseFromArray failed\n");
        *out_buf = NULL;
        return 0;
    }

    // Do a crossover if necessary, and decide whether to mutate or not
    std::string protobuf;
    data->max_size = max_size;

#ifdef USE_CROSSOVERS
    bool should_mutate = true;
    if (should_crossover(data, buf, buf_size, add_buf, add_buf_size)) {
        // Create a PDU object from the second protobuf data
        x509_certificate::SubjectPublicKeyInfo input2;
        debug_printf("[mutator] [afl_custom_fuzz] Converting second raw protobuf to SubjectPublicKeyInfo\n");
        if (unlikely(!input2.ParseFromArray(add_buf, add_buf_size))) {
            debug_printf("[mutator] [afl_custom_fuzz] /!\\ ParseFromArray failed, aborting crossover\n");
        } else {
            // Perform the crossover
            crossover(data->mutator, &input, &input2, &protobuf, max_size);

            // The resulting message may be too long, in which case we should
            // mutate the data until it's short enough
            // If it's already the right size, skip mutations
            if (protobuf.length() > max_size) {
                debug_printf("[mutator] [afl_custom_fuzz] Crossover buffer is too long, performing mutations\n");
            } else {
                should_mutate = false;
            }
        }
    }

    // Mutate the protobuf if necessary
    if (should_mutate) {
#else
    {
#endif
        mutate(data->mutator, &input, &protobuf, max_size);
    }

    // Copy the mutated data into a buffer which remains valid after this
    // function ends and that can be reused in between runs
    if (unlikely(!maybe_grow(BUF_PARAM(data, protobuf_out), protobuf.length()))) {
        *out_buf = NULL;
        perror("[mutator] [afl_custom_fuzz] maybe_grow failed");
        return 0;
    }
    memcpy(data->protobuf_out_buf, protobuf.c_str(), protobuf.length());

    debug_printf("[mutator] [afl_custom_fuzz] Generated mutated data %p of size %ld\n", data->protobuf_out_buf, protobuf.length());

    *out_buf = data->protobuf_out_buf;
    return protobuf.length();
}

/**
 * A post-processing function to use right before AFL writes the test case to
 * disk in order to execute the target.
 *
 * (Optional) If this functionality is not needed, simply don't define this
 * function.
 *
 * @param[in] data pointer returned in afl_custom_init for this fuzz case
 * @param[in] buf Buffer containing the test case to be executed
 * @param[in] buf_size Size of the test case
 * @param[out] out_buf Pointer to the buffer containing the test case after
 *     processing. External library should allocate memory for out_buf.
 *     The buf pointer may be reused (up to the given buf_size);
 * @return Size of the output buffer after processing or the needed amount.
 *     A return of 0 indicates an error.
 */
extern "C" size_t afl_custom_post_process(custom_mutator_t *data, uint8_t *buf,
                                          size_t buf_size, uint8_t **out_buf) {

    debug_printf("[mutator] [afl_custom_post_process] Got size: %ld, buf: %p\n", buf_size, buf);

    // Create a certificate object from the protobuf data
    x509_certificate::SubjectPublicKeyInfo input;
    debug_printf("[mutator] [afl_custom_post_process] Converting raw protobuf to SubjectPublicKeyInfo\n");
    if (unlikely(!input.ParseFromArray(buf, buf_size))) {
        printf("[mutator] [afl_custom_post_process] /!\\ ParseFromArray failed\n");
        *out_buf = NULL;
        return 0;
    }

    // Convert the protobuf data to ASN.1
    debug_printf("[mutator] [afl_custom_post_process] Converting SubjectPublicKeyInfo to DER\n");
    std::vector<uint8_t> asn1 = x509_certificate::SubjectPublicKeyInfoToDER(input);

    // Make sure our input isn't too long
    size_t new_size = asn1.size();
    if (likely(data->max_size > 0) && unlikely(new_size > data->max_size)) {
        debug_printf("[mutator] [afl_custom_post_process] Truncating output to %ld instead of %ld\n", data->max_size, new_size);
        new_size = data->max_size;
    }

    // Copy the converted data into a buffer which remains valid after this
    // function ends and that can be reused in between runs
    if (unlikely(!maybe_grow(BUF_PARAM(data, asn1_out), new_size))) {
        *out_buf = NULL;
        perror("[mutator] [afl_custom_post_process] maybe_grow failed");
        return 0;
    }

    memcpy(data->asn1_out_buf, asn1.data(), new_size);
    
    debug_printf("[mutator] [afl_custom_post_process] Generated ASN.1 data %p of size %ld\n", data->asn1_out_buf, new_size);

    *out_buf = data->asn1_out_buf;
    return new_size;
}

/**
 * Deinitialize everything
 *
 * @param data The data ptr from afl_custom_init
 */
extern "C" void afl_custom_deinit(custom_mutator_t *data) {
    debug_printf("[mutator] [afl_custom_deinit] Cleaning up\n");
    delete data->mutator;
    free(data->protobuf_out_buf);
    free(data->asn1_out_buf);
    free(data);
}
