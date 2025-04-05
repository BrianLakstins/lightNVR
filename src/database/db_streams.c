#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include <stdbool.h>

#include "database/db_streams.h"
#include "database/db_core.h"
#include "database/db_schema.h"
#include "database/db_schema_cache.h"
#include "core/logger.h"
#include "core/config.h"

/**
 * Add a stream configuration to the database
 *
 * @param stream Stream configuration to add
 * @return Stream ID on success, 0 on failure
 */
uint64_t add_stream_config(const stream_config_t *stream) {
    int rc;
    sqlite3_stmt *stmt;
    uint64_t stream_id = 0;

    sqlite3 *db = get_db_handle();
    pthread_mutex_t *db_mutex = get_db_mutex();

    if (!db) {
        log_error("Database not initialized");
        return 0;
    }

    if (!stream) {
        log_error("Stream configuration is required");
        return 0;
    }

    pthread_mutex_lock(db_mutex);

    // Check if a stream with this name already exists but is disabled
    const char *check_sql = "SELECT id FROM streams WHERE name = ? AND enabled = 0;";
    sqlite3_stmt *check_stmt;

    rc = sqlite3_prepare_v2(db, check_sql, -1, &check_stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("Failed to prepare statement to check for disabled stream: %s", sqlite3_errmsg(db));
        pthread_mutex_unlock(db_mutex);
        return 0;
    }

    sqlite3_bind_text(check_stmt, 1, stream->name, -1, SQLITE_STATIC);

    if (sqlite3_step(check_stmt) == SQLITE_ROW) {
        // Stream exists but is disabled, enable it by updating
        uint64_t existing_id = (uint64_t)sqlite3_column_int64(check_stmt, 0);

        // Finalize the prepared statement
        if (check_stmt) {
            sqlite3_finalize(check_stmt);
            check_stmt = NULL;
        }

        const char *update_sql = "UPDATE streams SET "
                                "url = ?, enabled = ?, streaming_enabled = ?, width = ?, height = ?, "
                                "fps = ?, codec = ?, priority = ?, record = ?, segment_duration = ?, "
                                "detection_based_recording = ?, detection_model = ?, detection_threshold = ?, "
                                "detection_interval = ?, pre_detection_buffer = ?, post_detection_buffer = ?, "
                                "protocol = ?, is_onvif = ?, record_audio = ? "
                                "WHERE id = ?;";

        rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            log_error("Failed to prepare statement to update disabled stream: %s", sqlite3_errmsg(db));
            pthread_mutex_unlock(db_mutex);
            return 0;
        }

        // Bind parameters for basic stream settings
        sqlite3_bind_text(stmt, 1, stream->url, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, stream->enabled ? 1 : 0);
        sqlite3_bind_int(stmt, 3, stream->streaming_enabled ? 1 : 0);
        sqlite3_bind_int(stmt, 4, stream->width);
        sqlite3_bind_int(stmt, 5, stream->height);
        sqlite3_bind_int(stmt, 6, stream->fps);
        sqlite3_bind_text(stmt, 7, stream->codec, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 8, stream->priority);
        sqlite3_bind_int(stmt, 9, stream->record ? 1 : 0);
        sqlite3_bind_int(stmt, 10, stream->segment_duration);

        // Bind parameters for detection settings
        sqlite3_bind_int(stmt, 11, stream->detection_based_recording ? 1 : 0);
        sqlite3_bind_text(stmt, 12, stream->detection_model, -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 13, stream->detection_threshold);
        sqlite3_bind_int(stmt, 14, stream->detection_interval);
        sqlite3_bind_int(stmt, 15, stream->pre_detection_buffer);
        sqlite3_bind_int(stmt, 16, stream->post_detection_buffer);

        // Bind protocol parameter
        sqlite3_bind_int(stmt, 17, (int)stream->protocol);

        // Bind is_onvif parameter
        sqlite3_bind_int(stmt, 18, stream->is_onvif ? 1 : 0);

        // Bind record_audio parameter
        sqlite3_bind_int(stmt, 19, stream->record_audio ? 1 : 0);

        // Bind ID parameter
        sqlite3_bind_int64(stmt, 20, (sqlite3_int64)existing_id);

        // Execute statement
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            log_error("Failed to update disabled stream configuration: %s", sqlite3_errmsg(db));

            // Finalize the prepared statement
            if (stmt) {
                sqlite3_finalize(stmt);
                stmt = NULL;
            }
            pthread_mutex_unlock(db_mutex);
            return 0;
        }

        // Finalize the prepared statement
        if (stmt) {
            sqlite3_finalize(stmt);
            stmt = NULL;
        }

        log_info("Updated disabled stream configuration: name=%s, enabled=%s, detection=%s, model=%s",
                stream->name,
                stream->enabled ? "true" : "false",
                stream->detection_based_recording ? "true" : "false",
                stream->detection_model);

        pthread_mutex_unlock(db_mutex);
        return existing_id;
    }

    // Finalize the prepared statement
    if (check_stmt) {
        sqlite3_finalize(check_stmt);
        check_stmt = NULL;
    }

    // No disabled stream found, insert a new one
    const char *sql = "INSERT INTO streams (name, url, enabled, streaming_enabled, width, height, fps, codec, priority, record, segment_duration, "
          "detection_based_recording, detection_model, detection_threshold, detection_interval, "
          "pre_detection_buffer, post_detection_buffer, protocol, is_onvif, record_audio) "
          "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("Failed to prepare statement: %s", sqlite3_errmsg(db));
        pthread_mutex_unlock(db_mutex);
        return 0;
    }

    // Bind parameters for basic stream settings
    sqlite3_bind_text(stmt, 1, stream->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, stream->url, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, stream->enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 4, stream->streaming_enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 5, stream->width);
    sqlite3_bind_int(stmt, 6, stream->height);
    sqlite3_bind_int(stmt, 7, stream->fps);
    sqlite3_bind_text(stmt, 8, stream->codec, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 9, stream->priority);
    sqlite3_bind_int(stmt, 10, stream->record ? 1 : 0);
    sqlite3_bind_int(stmt, 11, stream->segment_duration);

    // Bind parameters for detection settings
    sqlite3_bind_int(stmt, 12, stream->detection_based_recording ? 1 : 0);
    sqlite3_bind_text(stmt, 13, stream->detection_model, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 14, stream->detection_threshold);
    sqlite3_bind_int(stmt, 15, stream->detection_interval);
    sqlite3_bind_int(stmt, 16, stream->pre_detection_buffer);
    sqlite3_bind_int(stmt, 17, stream->post_detection_buffer);

    // Bind protocol parameter
    sqlite3_bind_int(stmt, 18, (int)stream->protocol);

    // Bind is_onvif parameter
    sqlite3_bind_int(stmt, 19, stream->is_onvif ? 1 : 0);

    // Bind record_audio parameter
    sqlite3_bind_int(stmt, 20, stream->record_audio ? 1 : 0);

    // Execute statement
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        log_error("Failed to add stream configuration: %s", sqlite3_errmsg(db));
        // Continue to finalize the statement
    } else {
        stream_id = (uint64_t)sqlite3_last_insert_rowid(db);
        log_debug("Added stream configuration with ID %llu", (unsigned long long)stream_id);

        // Log the addition
        log_info("Added stream configuration: name=%s, enabled=%s, detection=%s, model=%s",
                stream->name,
                stream->enabled ? "true" : "false",
                stream->detection_based_recording ? "true" : "false",
                stream->detection_model);
    }

    // Finalize the prepared statement
    if (stmt) {
        sqlite3_finalize(stmt);
        stmt = NULL;
    }
    pthread_mutex_unlock(db_mutex);

    return stream_id;
}

/**
 * Update a stream configuration in the database
 *
 * @param name Stream name to update
 * @param stream Updated stream configuration
 * @return 0 on success, non-zero on failure
 */
int update_stream_config(const char *name, const stream_config_t *stream) {
    int rc;
    sqlite3_stmt *stmt;

    sqlite3 *db = get_db_handle();
    pthread_mutex_t *db_mutex = get_db_mutex();

    if (!db) {
        log_error("Database not initialized");
        return -1;
    }

    if (!name || !stream) {
        log_error("Stream name and configuration are required");
        return -1;
    }

    pthread_mutex_lock(db_mutex);

    // Schema migrations should have already been run during database initialization
    // No need to check for columns here anymore

    // Now update the stream with all fields including detection settings, protocol, is_onvif, and record_audio
    const char *sql = "UPDATE streams SET "
                      "name = ?, url = ?, enabled = ?, streaming_enabled = ?, width = ?, height = ?, "
                      "fps = ?, codec = ?, priority = ?, record = ?, segment_duration = ?, "
                      "detection_based_recording = ?, detection_model = ?, detection_threshold = ?, "
                      "detection_interval = ?, pre_detection_buffer = ?, post_detection_buffer = ?, "
                      "protocol = ?, is_onvif = ?, record_audio = ? "
                      "WHERE name = ?;";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("Failed to prepare statement: %s", sqlite3_errmsg(db));
        pthread_mutex_unlock(db_mutex);
        return -1;
    }

    // Bind parameters for basic stream settings
    sqlite3_bind_text(stmt, 1, stream->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, stream->url, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, stream->enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 4, stream->streaming_enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 5, stream->width);
    sqlite3_bind_int(stmt, 6, stream->height);
    sqlite3_bind_int(stmt, 7, stream->fps);
    sqlite3_bind_text(stmt, 8, stream->codec, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 9, stream->priority);
    sqlite3_bind_int(stmt, 10, stream->record ? 1 : 0);
    sqlite3_bind_int(stmt, 11, stream->segment_duration);

    // Bind parameters for detection settings
    sqlite3_bind_int(stmt, 12, stream->detection_based_recording ? 1 : 0);
    sqlite3_bind_text(stmt, 13, stream->detection_model, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 14, stream->detection_threshold);
    sqlite3_bind_int(stmt, 15, stream->detection_interval);
    sqlite3_bind_int(stmt, 16, stream->pre_detection_buffer);
    sqlite3_bind_int(stmt, 17, stream->post_detection_buffer);

    // Bind protocol parameter
    sqlite3_bind_int(stmt, 18, (int)stream->protocol);

    // Bind is_onvif parameter
    sqlite3_bind_int(stmt, 19, stream->is_onvif ? 1 : 0);

    // Bind record_audio parameter
    sqlite3_bind_int(stmt, 20, stream->record_audio ? 1 : 0);

    // Bind the WHERE clause parameter
    sqlite3_bind_text(stmt, 21, name, -1, SQLITE_STATIC);

    // Execute statement
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        log_error("Failed to update stream configuration: %s", sqlite3_errmsg(db));

        // Finalize the prepared statement
        if (stmt) {
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
        pthread_mutex_unlock(db_mutex);
        return -1;
    }

    // Finalize the prepared statement
    if (stmt) {
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    // Log the update
    log_info("Updated stream configuration for %s: enabled=%s, detection=%s, model=%s",
             stream->name,
             stream->enabled ? "true" : "false",
             stream->detection_based_recording ? "true" : "false",
             stream->detection_model);

    pthread_mutex_unlock(db_mutex);

    return 0;
}

/**
 * Delete a stream configuration from the database
 *
 * @param name Stream name to delete
 * @return 0 on success, non-zero on failure
 */
int delete_stream_config(const char *name) {
    return delete_stream_config_internal(name, false);
}

/**
 * Delete a stream configuration from the database with option for permanent deletion
 *
 * @param name Stream name to delete
 * @param permanent If true, permanently delete the stream; if false, just disable it
 * @return 0 on success, non-zero on failure
 */
int delete_stream_config_internal(const char *name, bool permanent) {
    int rc;
    sqlite3_stmt *stmt;

    sqlite3 *db = get_db_handle();
    pthread_mutex_t *db_mutex = get_db_mutex();

    if (!db) {
        log_error("Database not initialized");
        return -1;
    }

    if (!name) {
        log_error("Stream name is required");
        return -1;
    }

    pthread_mutex_lock(db_mutex);

    const char *sql;
    if (permanent) {
        // Permanently delete the stream
        sql = "DELETE FROM streams WHERE name = ?;";
        log_info("Preparing to permanently delete stream: %s", name);
    } else {
        // Disable the stream by setting enabled = 0
        sql = "UPDATE streams SET enabled = 0 WHERE name = ?;";
        log_info("Preparing to disable stream: %s", name);
    }

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("Failed to prepare statement: %s", sqlite3_errmsg(db));
        pthread_mutex_unlock(db_mutex);
        return -1;
    }

    // Bind parameters
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

    // Execute statement
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        log_error("Failed to %s stream configuration: %s",
                permanent ? "permanently delete" : "disable",
                sqlite3_errmsg(db));

        // Finalize the prepared statement
        if (stmt) {
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
        pthread_mutex_unlock(db_mutex);
        return -1;
    }

    // Finalize the prepared statement
    if (stmt) {
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    if (permanent) {
        log_info("Permanently deleted stream configuration: %s", name);
    } else {
        log_info("Disabled stream configuration: %s", name);
    }

    pthread_mutex_unlock(db_mutex);

    return 0;
}

/**
 * Get a stream configuration from the database
 *
 * @param name Stream name to get
 * @param stream Stream configuration to fill
 * @return 0 on success, non-zero on failure
 */
int get_stream_config_by_name(const char *name, stream_config_t *stream) {
    int rc;
    sqlite3_stmt *stmt;
    int result = -1;

    sqlite3 *db = get_db_handle();
    pthread_mutex_t *db_mutex = get_db_mutex();

    if (!db) {
        log_error("Database not initialized");
        return -1;
    }

    if (!name || !stream) {
        log_error("Stream name and configuration pointer are required");
        return -1;
    }

    pthread_mutex_lock(db_mutex);

    // Use our cached schema management functions to check for columns
    bool has_detection_columns = cached_column_exists("streams", "detection_based_recording");
    bool has_protocol_column = cached_column_exists("streams", "protocol");
    bool has_onvif_column = cached_column_exists("streams", "is_onvif");
    bool has_record_audio_column = cached_column_exists("streams", "record_audio");

    // Prepare SQL based on whether detection columns, protocol column, is_onvif column, and record_audio column exist
    const char *sql;
    if (has_detection_columns && has_protocol_column && has_onvif_column && has_record_audio_column) {
        sql = "SELECT name, url, enabled, streaming_enabled, width, height, fps, codec, priority, record, segment_duration, "
              "detection_based_recording, detection_model, detection_threshold, detection_interval, "
              "pre_detection_buffer, post_detection_buffer, protocol, is_onvif, record_audio "
              "FROM streams WHERE name = ?;";
    } else if (has_detection_columns && has_protocol_column) {
        sql = "SELECT name, url, enabled, streaming_enabled, width, height, fps, codec, priority, record, segment_duration, "
              "detection_based_recording, detection_model, detection_threshold, detection_interval, "
              "pre_detection_buffer, post_detection_buffer, protocol "
              "FROM streams WHERE name = ?;";
    } else if (has_detection_columns) {
        sql = "SELECT name, url, enabled, streaming_enabled, width, height, fps, codec, priority, record, segment_duration, "
              "detection_based_recording, detection_model, detection_threshold, detection_interval, "
              "pre_detection_buffer, post_detection_buffer "
              "FROM streams WHERE name = ?;";
    } else {
        sql = "SELECT name, url, enabled, streaming_enabled, width, height, fps, codec, priority, record, segment_duration "
              "FROM streams WHERE name = ?;";
    }

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("Failed to prepare statement: %s", sqlite3_errmsg(db));
        pthread_mutex_unlock(db_mutex);
        return -1;
    }

    // Bind parameters
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

    // Execute query and fetch result
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        // Initialize stream with default values
        memset(stream, 0, sizeof(stream_config_t));

        // Set default values for detection settings
        stream->detection_threshold = 0.5f;
        stream->detection_interval = 10;
        stream->pre_detection_buffer = 0;  // Eliminated pre-buffering to prevent live stream delay
        stream->post_detection_buffer = 3;  // Reduced from 10 to 3 to decrease latency

        // Parse basic stream settings
        const char *stream_name = (const char *)sqlite3_column_text(stmt, 0);
        if (stream_name) {
            strncpy(stream->name, stream_name, MAX_STREAM_NAME - 1);
            stream->name[MAX_STREAM_NAME - 1] = '\0';
        }

        const char *url = (const char *)sqlite3_column_text(stmt, 1);
        if (url) {
            strncpy(stream->url, url, MAX_URL_LENGTH - 1);
            stream->url[MAX_URL_LENGTH - 1] = '\0';
        }

        stream->enabled = sqlite3_column_int(stmt, 2) != 0;
        stream->streaming_enabled = sqlite3_column_int(stmt, 3) != 0;
        stream->width = sqlite3_column_int(stmt, 4);
        stream->height = sqlite3_column_int(stmt, 5);
        stream->fps = sqlite3_column_int(stmt, 6);

        const char *codec = (const char *)sqlite3_column_text(stmt, 7);
        if (codec) {
            strncpy(stream->codec, codec, sizeof(stream->codec) - 1);
            stream->codec[sizeof(stream->codec) - 1] = '\0';
        }

        stream->priority = sqlite3_column_int(stmt, 8);
        stream->record = sqlite3_column_int(stmt, 9) != 0;
        stream->segment_duration = sqlite3_column_int(stmt, 10);

        // Parse detection settings if columns exist
        if (has_detection_columns && sqlite3_column_count(stmt) > 11) {
            stream->detection_based_recording = sqlite3_column_int(stmt, 11) != 0;

            const char *detection_model = (const char *)sqlite3_column_text(stmt, 12);
            if (detection_model) {
                strncpy(stream->detection_model, detection_model, MAX_PATH_LENGTH - 1);
                stream->detection_model[MAX_PATH_LENGTH - 1] = '\0';
            }

            if (sqlite3_column_type(stmt, 13) != SQLITE_NULL) {
                stream->detection_threshold = (float)sqlite3_column_double(stmt, 13);
            }

            if (sqlite3_column_type(stmt, 14) != SQLITE_NULL) {
                stream->detection_interval = sqlite3_column_int(stmt, 14);
            }

            if (sqlite3_column_type(stmt, 15) != SQLITE_NULL) {
                stream->pre_detection_buffer = sqlite3_column_int(stmt, 15);
            }

            if (sqlite3_column_type(stmt, 16) != SQLITE_NULL) {
                stream->post_detection_buffer = sqlite3_column_int(stmt, 16);
            }

            // Parse protocol if it exists (column 17)
            if (has_protocol_column && sqlite3_column_count(stmt) > 17) {
                if (sqlite3_column_type(stmt, 17) != SQLITE_NULL) {
                    stream->protocol = (stream_protocol_t)sqlite3_column_int(stmt, 17);
                }
            }

            // Parse is_onvif if it exists (column 18)
            if (has_onvif_column && sqlite3_column_count(stmt) > 18) {
                if (sqlite3_column_type(stmt, 18) != SQLITE_NULL) {
                    stream->is_onvif = sqlite3_column_int(stmt, 18) != 0;
                }
            }

            // Parse record_audio if it exists (column 19)
            if (has_record_audio_column && sqlite3_column_count(stmt) > 19) {
                if (sqlite3_column_type(stmt, 19) != SQLITE_NULL) {
                    stream->record_audio = sqlite3_column_int(stmt, 19) != 0;
                }
            }
        }

        result = 0; // Success
    }

    // Finalize the prepared statement
    if (stmt) {
        sqlite3_finalize(stmt);
        stmt = NULL;
    }
    pthread_mutex_unlock(db_mutex);

    return result;
}

/**
 * Get all stream configurations from the database
 *
 * @param streams Array to fill with stream configurations
 * @param max_count Maximum number of streams to return
 * @return Number of streams found, or -1 on error
 */
int get_all_stream_configs(stream_config_t *streams, int max_count) {
    int rc;
    sqlite3_stmt *stmt;
    int count = 0;

    sqlite3 *db = get_db_handle();
    pthread_mutex_t *db_mutex = get_db_mutex();

    if (!db) {
        log_error("Database not initialized");
        return -1;
    }

    if (!streams || max_count <= 0) {
        log_error("Invalid parameters for get_all_stream_configs");
        return -1;
    }

    pthread_mutex_lock(db_mutex);

    // Use our cached schema management functions to check for columns
    bool has_detection_columns = cached_column_exists("streams", "detection_based_recording");
    bool has_protocol_column = cached_column_exists("streams", "protocol");
    bool has_onvif_column = cached_column_exists("streams", "is_onvif");
    bool has_record_audio_column = cached_column_exists("streams", "record_audio");

    // Prepare SQL based on whether detection columns, protocol column, is_onvif column, and record_audio column exist
    const char *sql;
    if (has_detection_columns && has_protocol_column && has_onvif_column && has_record_audio_column) {
        sql = "SELECT name, url, enabled, streaming_enabled, width, height, fps, codec, priority, record, segment_duration, "
              "detection_based_recording, detection_model, detection_threshold, detection_interval, "
              "pre_detection_buffer, post_detection_buffer, protocol, is_onvif, record_audio "
              "FROM streams ORDER BY name;";
    } else if (has_detection_columns && has_protocol_column) {
        sql = "SELECT name, url, enabled, streaming_enabled, width, height, fps, codec, priority, record, segment_duration, "
              "detection_based_recording, detection_model, detection_threshold, detection_interval, "
              "pre_detection_buffer, post_detection_buffer, protocol "
              "FROM streams ORDER BY name;";
    } else if (has_detection_columns) {
        sql = "SELECT name, url, enabled, streaming_enabled, width, height, fps, codec, priority, record, segment_duration, "
              "detection_based_recording, detection_model, detection_threshold, detection_interval, "
              "pre_detection_buffer, post_detection_buffer "
              "FROM streams ORDER BY name;";
    } else {
        sql = "SELECT name, url, enabled, streaming_enabled, width, height, fps, codec, priority, record, segment_duration "
              "FROM streams ORDER BY name;";
    }

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("Failed to prepare statement: %s", sqlite3_errmsg(db));
        pthread_mutex_unlock(db_mutex);
        return -1;
    }

    // Execute query and fetch results
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        // Initialize stream with default values
        memset(&streams[count], 0, sizeof(stream_config_t));

        // Set default values for detection settings
        streams[count].detection_threshold = 0.5f;
        streams[count].detection_interval = 10;
        streams[count].pre_detection_buffer = 0;  // Eliminated pre-buffering to prevent live stream delay
        streams[count].post_detection_buffer = 3;  // Reduced from 10 to 3 to decrease latency

        // Parse basic stream settings
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        if (name) {
            strncpy(streams[count].name, name, MAX_STREAM_NAME - 1);
            streams[count].name[MAX_STREAM_NAME - 1] = '\0';
        }

        const char *url = (const char *)sqlite3_column_text(stmt, 1);
        if (url) {
            strncpy(streams[count].url, url, MAX_URL_LENGTH - 1);
            streams[count].url[MAX_URL_LENGTH - 1] = '\0';
        }

        streams[count].enabled = sqlite3_column_int(stmt, 2) != 0;
        streams[count].streaming_enabled = sqlite3_column_int(stmt, 3) != 0;
        streams[count].width = sqlite3_column_int(stmt, 4);
        streams[count].height = sqlite3_column_int(stmt, 5);
        streams[count].fps = sqlite3_column_int(stmt, 6);

        const char *codec = (const char *)sqlite3_column_text(stmt, 7);
        if (codec) {
            strncpy(streams[count].codec, codec, sizeof(streams[count].codec) - 1);
            streams[count].codec[sizeof(streams[count].codec) - 1] = '\0';
        }

        streams[count].priority = sqlite3_column_int(stmt, 8);
        streams[count].record = sqlite3_column_int(stmt, 9) != 0;
        streams[count].segment_duration = sqlite3_column_int(stmt, 10);

        // Parse detection settings if columns exist
        if (has_detection_columns && sqlite3_column_count(stmt) > 11) {
            streams[count].detection_based_recording = sqlite3_column_int(stmt, 11) != 0;

            const char *detection_model = (const char *)sqlite3_column_text(stmt, 12);
            if (detection_model) {
                strncpy(streams[count].detection_model, detection_model, MAX_PATH_LENGTH - 1);
                streams[count].detection_model[MAX_PATH_LENGTH - 1] = '\0';
            }

            if (sqlite3_column_type(stmt, 13) != SQLITE_NULL) {
                streams[count].detection_threshold = (float)sqlite3_column_double(stmt, 13);
            }

            if (sqlite3_column_type(stmt, 14) != SQLITE_NULL) {
                streams[count].detection_interval = sqlite3_column_int(stmt, 14);
            }

            if (sqlite3_column_type(stmt, 15) != SQLITE_NULL) {
                streams[count].pre_detection_buffer = sqlite3_column_int(stmt, 15);
            }

            if (sqlite3_column_type(stmt, 16) != SQLITE_NULL) {
                streams[count].post_detection_buffer = sqlite3_column_int(stmt, 16);
            }

            // Parse protocol if it exists (column 17)
            if (has_protocol_column && sqlite3_column_count(stmt) > 17) {
                if (sqlite3_column_type(stmt, 17) != SQLITE_NULL) {
                    streams[count].protocol = (stream_protocol_t)sqlite3_column_int(stmt, 17);
                }
            }

            // Parse is_onvif if it exists (column 18)
            if (has_onvif_column && sqlite3_column_count(stmt) > 18) {
                if (sqlite3_column_type(stmt, 18) != SQLITE_NULL) {
                    streams[count].is_onvif = sqlite3_column_int(stmt, 18) != 0;
                }
            }

            // Parse record_audio if it exists (column 19)
            if (has_record_audio_column && sqlite3_column_count(stmt) > 19) {
                if (sqlite3_column_type(stmt, 19) != SQLITE_NULL) {
                    streams[count].record_audio = sqlite3_column_int(stmt, 19) != 0;
                }
            }
        }

        count++;
    }

    // Finalize the prepared statement
    if (stmt) {
        sqlite3_finalize(stmt);
        stmt = NULL;
    }
    pthread_mutex_unlock(db_mutex);

    return count;
}

/**
 * Check if a stream is eligible for live streaming
 *
 * @param stream_name Name of the stream to check
 * @return 1 if eligible, 0 if not eligible, -1 on error
 */
int is_stream_eligible_for_live_streaming(const char *stream_name) {
    int rc;
    sqlite3_stmt *stmt;
    int result = -1;

    sqlite3 *db = get_db_handle();
    pthread_mutex_t *db_mutex = get_db_mutex();

    if (!db) {
        log_error("Database not initialized");
        return -1;
    }

    if (!stream_name) {
        log_error("Stream name is required");
        return -1;
    }

    pthread_mutex_lock(db_mutex);

    const char *sql = "SELECT enabled, streaming_enabled FROM streams WHERE name = ?;";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("Failed to prepare statement: %s", sqlite3_errmsg(db));
        pthread_mutex_unlock(db_mutex);
        return -1;
    }

    // Bind parameters
    sqlite3_bind_text(stmt, 1, stream_name, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        bool enabled = sqlite3_column_int(stmt, 0) != 0;
        bool streaming_enabled = sqlite3_column_int(stmt, 1) != 0;

        // Stream is eligible if it's enabled and streaming is enabled
        result = (enabled && streaming_enabled) ? 1 : 0;

        if (!enabled) {
            log_info("Stream %s is not eligible for live streaming: not enabled", stream_name);
        } else if (!streaming_enabled) {
            log_info("Stream %s is not eligible for live streaming: streaming not enabled", stream_name);
        }
    } else {
        log_error("Stream %s not found", stream_name);
        result = 0; // Not eligible if not found
    }

    // Finalize the prepared statement
    if (stmt) {
        sqlite3_finalize(stmt);
        stmt = NULL;
    }
    pthread_mutex_unlock(db_mutex);

    return result;
}

/**
 * Count the number of enabled stream configurations in the database
 *
 * @return Number of enabled streams, or -1 on error
 */
int get_enabled_stream_count(void) {
    int rc;
    sqlite3_stmt *stmt;
    int count = -1;

    sqlite3 *db = get_db_handle();
    pthread_mutex_t *db_mutex = get_db_mutex();

    if (!db) {
        log_error("Database not initialized");
        return -1;
    }

    pthread_mutex_lock(db_mutex);

    const char *sql = "SELECT COUNT(*) FROM streams WHERE enabled = 1;";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("Failed to prepare statement: %s", sqlite3_errmsg(db));
        pthread_mutex_unlock(db_mutex);
        return -1;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    // finalize the prepared statement
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(db_mutex);

    return count;
}

/**
 * Count the number of stream configurations in the database
 *
 * @return Number of streams, or -1 on error
 */
int count_stream_configs(void) {
    int rc;
    sqlite3_stmt *stmt;
    int count = -1;

    sqlite3 *db = get_db_handle();
    pthread_mutex_t *db_mutex = get_db_mutex();

    if (!db) {
        log_error("Database not initialized");
        return -1;
    }

    pthread_mutex_lock(db_mutex);

    const char *sql = "SELECT COUNT(*) FROM streams;";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("Failed to prepare statement: %s", sqlite3_errmsg(db));
        pthread_mutex_unlock(db_mutex);
        return -1;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }

    // finalize the prepared statement
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(db_mutex);

    return count;
}
