#include "../src/value_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define DEFAULT_EPOCHS 10
#define DEFAULT_LEARNING_RATE 0.01f
#define DEFAULT_L2 0.00001f
#define LINE_BUFFER_SIZE 32768
#define DEFAULT_TRAINING_LOG_PATH "train_value_v2.csv"
#define DATASET_SCHEMA_PREFIX "#gungi_value_schema,"

typedef struct Dataset {
    float *features;
    float *targets;
    int count;
    int capacity;
    int schema_version;
} Dataset;

static void ensure_models_dir(void)
{
#ifdef _WIN32
    _mkdir("models");
#else
    mkdir("models", 0755);
#endif
}

static int reserve_dataset(Dataset *data, int needed)
{
    int new_capacity;
    float *new_features;
    float *new_targets;

    if (data->capacity >= needed) {
        return 1;
    }

    new_capacity = data->capacity == 0 ? 1024 : data->capacity * 2;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    new_features = (float *)realloc(data->features,
                                    (size_t)new_capacity * GUNGI_VALUE_FEATURE_COUNT * sizeof(float));
    if (new_features == NULL) {
        return 0;
    }
    data->features = new_features;

    new_targets = (float *)realloc(data->targets, (size_t)new_capacity * sizeof(float));
    if (new_targets == NULL) {
        return 0;
    }
    data->targets = new_targets;
    data->capacity = new_capacity;
    return 1;
}

static int parse_row(char *line, Dataset *data)
{
    char *cursor = line;
    char *end = NULL;
    int i;

    if (line == NULL || line[0] == '\0') {
        return 1;
    }
    if (strncmp(line, DATASET_SCHEMA_PREFIX, strlen(DATASET_SCHEMA_PREFIX)) == 0) {
        data->schema_version = atoi(line + strlen(DATASET_SCHEMA_PREFIX));
        return 1;
    }
    if (line[0] == '#' || line[0] == 't') {
        return 1;
    }

    if (!reserve_dataset(data, data->count + 1)) {
        return 0;
    }

    data->targets[data->count] = (float)strtod(cursor, &end);
    if (end == cursor) {
        return 1;
    }
    cursor = end;

    for (i = 0; i < GUNGI_VALUE_FEATURE_COUNT; ++i) {
        if (*cursor == ',') {
            cursor++;
        }
        data->features[(size_t)data->count * GUNGI_VALUE_FEATURE_COUNT + i] = (float)strtod(cursor, &end);
        if (end == cursor) {
            return 1;
        }
        cursor = end;
    }

    data->count++;
    return 1;
}

static int load_dataset(const char *path, Dataset *data)
{
    FILE *file = fopen(path, "r");
    char line[LINE_BUFFER_SIZE];

    if (file == NULL) {
        fprintf(stderr, "Failed to open %s\n", path);
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        if (!parse_row(line, data)) {
            fclose(file);
            return 0;
        }
    }

    fclose(file);
    if (data->schema_version != GUNGI_VALUE_MODEL_VERSION) {
        fprintf(stderr,
                "Dataset schema mismatch in %s: expected %d got %d\n",
                path,
                GUNGI_VALUE_MODEL_VERSION,
                data->schema_version);
        return 0;
    }
    return data->count > 0;
}

static float predict_row(const GungiValueModel *model, const float *features)
{
    float score = 0.0f;
    int i;

    for (i = 0; i < GUNGI_VALUE_FEATURE_COUNT; ++i) {
        score += model->weights[i] * features[i];
    }

    return (float)tanh((double)score);
}

static float mse_for_range(const Dataset *data, const GungiValueModel *model, const int *indices, int start, int end)
{
    double total = 0.0;
    int count = end - start;
    int i;

    if (count <= 0) {
        return 0.0f;
    }

    for (i = start; i < end; ++i) {
        int row = indices[i];
        const float *features = &data->features[(size_t)row * GUNGI_VALUE_FEATURE_COUNT];
        float prediction = predict_row(model, features);
        float error = data->targets[row] - prediction;
        total += (double)error * (double)error;
    }

    return (float)(total / (double)count);
}

static void shuffle_indices(int *indices, int count)
{
    int i;

    for (i = count - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }
}

static void write_log_header(FILE *log)
{
    fprintf(log,
            "run_timestamp,model_version,feature_count,data_path,model_path,rows,train_rows,val_rows,epochs,learning_rate,l2,epoch,train_mse,val_mse\n");
}

int main(int argc, char **argv)
{
    const char *data_path = argc > 1 ? argv[1] : "value_data.csv";
    const char *model_path = argc > 2 ? argv[2] : GUNGI_VALUE_DEFAULT_MODEL_PATH;
    int epochs = argc > 3 ? atoi(argv[3]) : DEFAULT_EPOCHS;
    float learning_rate = argc > 4 ? (float)atof(argv[4]) : DEFAULT_LEARNING_RATE;
    float l2 = argc > 5 ? (float)atof(argv[5]) : DEFAULT_L2;
    const char *log_path = argc > 6 ? argv[6] : DEFAULT_TRAINING_LOG_PATH;
    long run_timestamp = (long)time(NULL);
    Dataset data;
    GungiValueModel model;
    float best_weights[GUNGI_VALUE_FEATURE_COUNT];
    FILE *log_file;
    int *indices;
    int train_count;
    int val_count;
    int epoch;
    int i;
    float best_val_mse = 0.0f;
    int best_epoch = 0;

    memset(&data, 0, sizeof(data));
    if (epochs <= 0) {
        epochs = DEFAULT_EPOCHS;
    }
    if (learning_rate <= 0.0f) {
        learning_rate = DEFAULT_LEARNING_RATE;
    }

    if (!load_dataset(data_path, &data)) {
        fprintf(stderr, "No training rows loaded from %s\n", data_path);
        free(data.features);
        free(data.targets);
        return 1;
    }

    indices = (int *)malloc((size_t)data.count * sizeof(int));
    if (indices == NULL) {
        fprintf(stderr, "Failed to allocate index buffer\n");
        free(data.features);
        free(data.targets);
        return 1;
    }
    for (i = 0; i < data.count; ++i) {
        indices[i] = i;
    }

    srand(1);
    shuffle_indices(indices, data.count);
    train_count = data.count * 9 / 10;
    if (train_count <= 0) {
        train_count = data.count;
    }
    val_count = data.count - train_count;

    gungi_value_init(&model);
    model.loaded = 1;
    memset(best_weights, 0, sizeof(best_weights));

    log_file = fopen(log_path, "w");
    if (log_file == NULL) {
        fprintf(stderr, "Failed to open training log %s\n", log_path);
        free(indices);
        free(data.features);
        free(data.targets);
        return 1;
    }
    write_log_header(log_file);

    printf("Training value model: rows=%d train=%d val=%d epochs=%d lr=%.6f l2=%.8f log=%s\n",
           data.count, train_count, data.count - train_count, epochs, learning_rate, l2, log_path);

    for (epoch = 1; epoch <= epochs; ++epoch) {
        float train_mse;
        float val_mse;
        float monitor_mse;

        shuffle_indices(indices, train_count);

        for (i = 0; i < train_count; ++i) {
            int row = indices[i];
            const float *features = &data.features[(size_t)row * GUNGI_VALUE_FEATURE_COUNT];
            float prediction = predict_row(&model, features);
            float error = data.targets[row] - prediction;
            float grad_scale = error * (1.0f - prediction * prediction);
            int f;

            for (f = 0; f < GUNGI_VALUE_FEATURE_COUNT; ++f) {
                model.weights[f] += learning_rate * grad_scale * features[f];
                model.weights[f] -= learning_rate * l2 * model.weights[f];
            }
        }

        train_mse = mse_for_range(&data, &model, indices, 0, train_count);
        val_mse = mse_for_range(&data, &model, indices, train_count, data.count);
        monitor_mse = val_count > 0 ? val_mse : train_mse;
        if (best_epoch == 0 || monitor_mse < best_val_mse) {
            best_val_mse = monitor_mse;
            best_epoch = epoch;
            memcpy(best_weights, model.weights, sizeof(best_weights));
        }
        printf("epoch %d train_mse=%.8f val_mse=%.8f\n",
               epoch,
               train_mse,
               val_mse);
        fprintf(log_file,
                "%ld,%d,%d,%s,%s,%d,%d,%d,%d,%.8f,%.10f,%d,%.8f,%.8f\n",
                run_timestamp,
                GUNGI_VALUE_MODEL_VERSION,
                GUNGI_VALUE_FEATURE_COUNT,
                data_path,
                model_path,
                data.count,
                train_count,
                data.count - train_count,
                epochs,
                learning_rate,
                l2,
                epoch,
                train_mse,
                val_mse);
        fflush(log_file);
    }

    ensure_models_dir();
    memcpy(model.weights, best_weights, sizeof(model.weights));
    if (!gungi_value_save(&model, model_path)) {
        fprintf(stderr, "Failed to save %s\n", model_path);
        fclose(log_file);
        free(indices);
        free(data.features);
        free(data.targets);
        return 1;
    }

    printf("Saved %s from best_epoch=%d best_%s_mse=%.8f\n",
           model_path,
           best_epoch,
           val_count > 0 ? "val" : "train",
           best_val_mse);
    printf("Training log saved %s\n", log_path);
    fclose(log_file);
    free(indices);
    free(data.features);
    free(data.targets);
    return 0;
}
