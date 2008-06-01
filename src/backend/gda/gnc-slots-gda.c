/********************************************************************
 * gnc-slots-gda.c: load and save data to SQL via libgda            *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
\********************************************************************/
/** @file gnc-slots-gda.c
 *  @brief load and save data to SQL 
 *  @author Copyright (c) 2006 Phil Longstaff <plongstaff@rogers.com>
 *
 * This file implements the top-level QofBackend API for saving/
 * restoring data to/from an SQL db using libgda
 */

#include "config.h"

#include <glib.h>
#include <libgda/libgda.h>

#include "qof.h"
#include "gnc-engine.h"

#include "gnc-backend-util-gda.h"

#include "gnc-slots-gda.h"

static QofLogModule log_module = G_LOG_DOMAIN;

#define TABLE_NAME "slots"
#define TABLE_VERSION 1

typedef struct {
    GncGdaBackend* be;
    const GUID* guid;
    KvpFrame* pKvpFrame;
    KvpValueType value_type;
    KvpValue* pKvpValue;
    GString* path;
} slot_info_t;

static gpointer get_obj_guid( gpointer pObject, const QofParam* param );
static void set_obj_guid( gpointer pObject, gpointer pValue );
static gpointer get_path( gpointer pObject, const QofParam* param );
static void set_path( gpointer pObject, gpointer pValue );
static gpointer get_slot_type( gpointer pObject, const QofParam* param );
static void set_slot_type( gpointer pObject, gpointer pValue );
static gint64 get_int64_val( gpointer pObject, const QofParam* param );
static void set_int64_val( gpointer pObject, gint64 pValue );
static gpointer get_string_val( gpointer pObject, const QofParam* param );
static void set_string_val( gpointer pObject, gpointer pValue );
static gpointer get_double_val( gpointer pObject, const QofParam* param );
static void set_double_val( gpointer pObject, gpointer pValue );
static Timespec get_timespec_val( gpointer pObject, const QofParam* param );
static void set_timespec_val( gpointer pObject, Timespec ts );
static gpointer get_guid_val( gpointer pObject, const QofParam* param );
static void set_guid_val( gpointer pObject, gpointer pValue );
static gnc_numeric get_numeric_val( gpointer pObject, const QofParam* param );
static void set_numeric_val( gpointer pObject, gnc_numeric value );

#define SLOT_MAX_PATHNAME_LEN 4096
#define SLOT_MAX_STRINGVAL_LEN 4096

static const col_cvt_t col_table[] =
{
    { "obj_guid",     CT_GUID,     0,                     COL_NNUL, NULL, NULL,
			get_obj_guid,     set_obj_guid },
    { "name",         CT_STRING,   SLOT_MAX_PATHNAME_LEN, COL_NNUL, NULL, NULL,
			get_path,         set_path },
    { "slot_type",    CT_INT,      0,                     COL_NNUL, NULL, NULL,
			get_slot_type,    set_slot_type, },
    { "int64_val",    CT_INT64,    0,                     0,        NULL, NULL,
			(QofAccessFunc)get_int64_val,    (QofSetterFunc)set_int64_val },
    { "string_val",   CT_STRING,   SLOT_MAX_PATHNAME_LEN, 0,        NULL, NULL,
			get_string_val,   set_string_val },
    { "double_val",   CT_DOUBLE,   0,                     0,        NULL, NULL,
			get_double_val,   set_double_val },
    { "timespec_val", CT_TIMESPEC, 0,                     0,        NULL, NULL,
			(QofAccessFunc)get_timespec_val, (QofSetterFunc)set_timespec_val },
    { "guid_val",     CT_GUID,     0,                     0,        NULL, NULL,
			get_guid_val,     set_guid_val },
    { "numeric_val",  CT_NUMERIC,  0,                     0,        NULL, NULL,
			(QofAccessFunc)get_numeric_val, (QofSetterFunc)set_numeric_val },
    { NULL }
};

/* Special column table because we need to be able to access the table by
a column other than the primary key */
static const col_cvt_t obj_guid_col_table[] =
{
    { "obj_guid", CT_GUID, 0, 0, NULL, NULL, get_obj_guid, _retrieve_guid_ },
    { NULL }
};

/* ================================================================= */

static gpointer
get_obj_guid( gpointer pObject, const QofParam* param )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_val_if_fail( pObject != NULL, NULL );

    return (gpointer)pInfo->guid;
}

static void
set_obj_guid( gpointer pObject, gpointer pValue )
{
    // Nowhere to put the GUID
}

static gpointer
get_path( gpointer pObject, const QofParam* param )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_val_if_fail( pObject != NULL, NULL );

    return (gpointer)pInfo->path->str;
}

static void
set_path( gpointer pObject, gpointer pValue )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_if_fail( pObject != NULL );
	g_return_if_fail( pValue != NULL );

    pInfo->path = g_string_new( (gchar*)pValue );
}

static gpointer
get_slot_type( gpointer pObject, const QofParam* param )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_val_if_fail( pObject != NULL, NULL );

    return (gpointer)kvp_value_get_type( pInfo->pKvpValue );
}

static void
set_slot_type( gpointer pObject, gpointer pValue )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_if_fail( pObject != NULL );
	g_return_if_fail( pValue != NULL );

    pInfo->value_type = (KvpValueType)pValue;
}

static gint64
get_int64_val( gpointer pObject, const QofParam* param )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_val_if_fail( pObject != NULL, 0 );

    if( kvp_value_get_type( pInfo->pKvpValue ) == KVP_TYPE_GINT64 ) {
        return kvp_value_get_gint64( pInfo->pKvpValue );
    } else {
        return 0;
    }
}

static void
set_int64_val( gpointer pObject, gint64 value )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_if_fail( pObject != NULL );

    if( pInfo->value_type == KVP_TYPE_GINT64 ) {
        kvp_frame_set_gint64( pInfo->pKvpFrame, pInfo->path->str, value );
    }
}

static gpointer
get_string_val( gpointer pObject, const QofParam* param )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_val_if_fail( pObject != NULL, NULL );

    if( kvp_value_get_type( pInfo->pKvpValue ) == KVP_TYPE_STRING ) {
        return (gpointer)kvp_value_get_string( pInfo->pKvpValue );
    } else {
        return NULL;
    }
}

static void
set_string_val( gpointer pObject, gpointer pValue )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_if_fail( pObject != NULL );

    if( pInfo->value_type == KVP_TYPE_STRING && pValue != NULL ) {
        kvp_frame_set_string( pInfo->pKvpFrame, pInfo->path->str, (const gchar*)pValue );
    }
}

static gpointer
get_double_val( gpointer pObject, const QofParam* param )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;
    static double d_val;

	g_return_val_if_fail( pObject != NULL, NULL );

    if( kvp_value_get_type( pInfo->pKvpValue ) == KVP_TYPE_DOUBLE ) {
        d_val = kvp_value_get_double( pInfo->pKvpValue );
        return (gpointer)&d_val;
    } else {
        return NULL;
    }
}

static void
set_double_val( gpointer pObject, gpointer pValue )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_if_fail( pObject != NULL );

    if( pInfo->value_type == KVP_TYPE_DOUBLE && pValue != NULL ) {
        kvp_frame_set_double( pInfo->pKvpFrame, pInfo->path->str, *(double*)pValue );
    }
}

static Timespec
get_timespec_val( gpointer pObject, const QofParam* param )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_val_if_fail( pObject != NULL, gnc_dmy2timespec( 1, 1, 1970 ) );

//if( kvp_value_get_type( pInfo->pKvpValue ) == KVP_TYPE_TIMESPEC ) {
    return kvp_value_get_timespec( pInfo->pKvpValue );
}

static void
set_timespec_val( gpointer pObject, Timespec ts )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_if_fail( pObject != NULL );

    if( pInfo->value_type == KVP_TYPE_TIMESPEC ) {
        kvp_frame_set_timespec( pInfo->pKvpFrame, pInfo->path->str, ts );
    }
}

static gpointer
get_guid_val( gpointer pObject, const QofParam* param )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_val_if_fail( pObject != NULL, NULL );

    if( kvp_value_get_type( pInfo->pKvpValue ) == KVP_TYPE_GUID ) {
        return (gpointer)kvp_value_get_guid( pInfo->pKvpValue );
    } else {
        return NULL;
    }
}

static void
set_guid_val( gpointer pObject, gpointer pValue )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_if_fail( pObject != NULL );

    if( pInfo->value_type == KVP_TYPE_GUID && pValue != NULL ) {
        kvp_frame_set_guid( pInfo->pKvpFrame, pInfo->path->str, (GUID*)pValue );
    }
}

static gnc_numeric
get_numeric_val( gpointer pObject, const QofParam* param )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_val_if_fail( pObject != NULL, gnc_numeric_zero() );

    if( kvp_value_get_type( pInfo->pKvpValue ) == KVP_TYPE_NUMERIC ) {
        return kvp_value_get_numeric( pInfo->pKvpValue );
    } else {
        return gnc_numeric_zero();
    }
}

static void
set_numeric_val( gpointer pObject, gnc_numeric value )
{
    slot_info_t* pInfo = (slot_info_t*)pObject;

	g_return_if_fail( pObject != NULL );

    if( pInfo->value_type == KVP_TYPE_NUMERIC ) {
        kvp_frame_set_numeric( pInfo->pKvpFrame, pInfo->path->str, value );
    }
}

static void
save_slot( const gchar* key, KvpValue* value, gpointer data )
{
    slot_info_t* pSlot_info = (slot_info_t*)data;
    gint curlen;

	g_return_if_fail( key != NULL );
	g_return_if_fail( value != NULL );
	g_return_if_fail( data != NULL );

    curlen = pSlot_info->path->len;
    pSlot_info->pKvpValue = value;
    if( curlen != 0 ) {
        g_string_append( pSlot_info->path, "/" );
    }
    g_string_append( pSlot_info->path, key );

    if( kvp_value_get_type( value ) == KVP_TYPE_FRAME ) {
        KvpFrame* pKvpFrame = kvp_value_get_frame( value );
        kvp_frame_for_each_slot( pKvpFrame, save_slot, pSlot_info );
    } else {
        (void)gnc_gda_do_db_operation( pSlot_info->be, OP_DB_ADD, TABLE_NAME,
                                        TABLE_NAME, pSlot_info, col_table );
    }

    g_string_truncate( pSlot_info->path, curlen );
}

void
gnc_gda_slots_save( GncGdaBackend* be, const GUID* guid, KvpFrame* pFrame )
{
    slot_info_t slot_info;

	g_return_if_fail( be != NULL );
	g_return_if_fail( guid != NULL );
	g_return_if_fail( pFrame != NULL );

    // If this is not saving into a new db, clear out the old saved slots first
	if( !be->is_pristine_db ) {
    	gnc_gda_slots_delete( be, guid );
	}

    slot_info.be = be;
    slot_info.guid = guid;
    slot_info.path = g_string_new( "" );
    kvp_frame_for_each_slot( pFrame, save_slot, &slot_info );
    g_string_free( slot_info.path, FALSE );
}

void
gnc_gda_slots_delete( GncGdaBackend* be, const GUID* guid )
{
    slot_info_t slot_info;

	g_return_if_fail( be != NULL );
	g_return_if_fail( guid != NULL );

    slot_info.be = be;
    slot_info.guid = guid;
    (void)gnc_gda_do_db_operation( be, OP_DB_DELETE, TABLE_NAME,
                                TABLE_NAME, &slot_info, obj_guid_col_table );
}

static void
load_slot( GncGdaBackend* be, GdaDataModel* pModel, gint row, KvpFrame* pFrame )
{
    slot_info_t slot_info;

	g_return_if_fail( be != NULL );
	g_return_if_fail( pModel != NULL );
	g_return_if_fail( row >= 0 );
	g_return_if_fail( pFrame != NULL );

    slot_info.be = be;
    slot_info.pKvpFrame = pFrame;
    slot_info.path = NULL;

    gnc_gda_load_object( be, pModel, row, TABLE_NAME, &slot_info, col_table );

    if( slot_info.path != NULL ) {
        g_string_free( slot_info.path, TRUE );
    }
}

void
gnc_gda_slots_load( GncGdaBackend* be, QofInstance* inst )
{
    gchar* buf;
    GdaObject* ret;
    gchar guid_buf[GUID_ENCODING_LENGTH+1];
    gchar* field_name;
    static GdaQuery* query = NULL;
    GdaQueryCondition* cond;
    GdaQueryField* key_value;
    GValue value;
	const GUID* guid;
	KvpFrame* pFrame;

	g_return_if_fail( be != NULL );
	g_return_if_fail( inst != NULL );

	guid = qof_instance_get_guid( inst );
	pFrame = qof_instance_get_slots( inst );
    guid_to_string_buff( guid, guid_buf );

    /* First time, create the query */
    if( query == NULL ) {
        GdaQueryTarget* target;
        GdaQueryField* key;

        /* SELECT */
        query = gnc_gda_create_select_query( be, TABLE_NAME );
        target = gda_query_get_target_by_alias( query, TABLE_NAME );

        /* WHERE */
        cond = gda_query_condition_new( query, GDA_QUERY_CONDITION_LEAF_EQUAL );
        gda_query_set_condition( query, cond );

        field_name = g_strdup_printf( "%s.%s",
                        gda_query_target_get_alias( target ), "obj_guid" );
        key = gda_query_field_field_new( query, field_name );
        g_free( field_name );
        gda_query_field_set_visible( key, TRUE );
        gda_query_condition_leaf_set_operator( cond,
                                                GDA_QUERY_CONDITION_OP_LEFT,
                                                GDA_QUERY_FIELD(key) );
        g_object_unref( G_OBJECT(key) );

        key_value = gda_query_field_value_new( query, G_TYPE_STRING );
        gda_query_field_set_visible( key_value, TRUE );
        gda_query_condition_leaf_set_operator( cond, GDA_QUERY_CONDITION_OP_RIGHT,
                                                GDA_QUERY_FIELD(key_value) );
        g_object_unref( G_OBJECT(key_value) );
    }

    /* Fill in the guid value */
    cond = gda_query_get_condition( query );
    key_value = gda_query_condition_leaf_get_operator( cond, 
                                                GDA_QUERY_CONDITION_OP_RIGHT );
    memset( &value, 0, sizeof( value ) );
    g_value_init( &value, G_TYPE_STRING );
    g_value_set_string( &value, guid_buf );
    gda_query_field_value_set_value( GDA_QUERY_FIELD_VALUE(key_value), &value );

	ret = gnc_gda_execute_query( be, query );
    if( GDA_IS_DATA_MODEL( ret ) ) {
        GdaDataModel* pModel = GDA_DATA_MODEL(ret);
        int numRows = gda_data_model_get_n_rows( pModel );
        int r;

        for( r = 0; r < numRows; r++ ) {
            load_slot( be, pModel, r, pFrame );
        }
    }
}

static const GUID*
load_obj_guid( const GncGdaBackend* be, GdaDataModel* pModel, gint row )
{
    static GUID guid;

	g_return_val_if_fail( be != NULL, NULL );
	g_return_val_if_fail( pModel != NULL, NULL );
	g_return_val_if_fail( row >= 0, NULL );

    gnc_gda_load_object( be, pModel, row, NULL, &guid, obj_guid_col_table );

    return &guid;
}

static void
load_slot_for_list_item( GncGdaBackend* be, GdaDataModel* pModel, gint row, QofCollection* coll )
{
    slot_info_t slot_info;
	const GUID* guid;
	QofInstance* inst;

	g_return_if_fail( be != NULL );
	g_return_if_fail( pModel != NULL );
	g_return_if_fail( row >= 0 );
	g_return_if_fail( coll != NULL );

	guid = load_obj_guid( be, pModel, row );
	inst = qof_collection_lookup_entity( coll, guid );

    slot_info.be = be;
    slot_info.pKvpFrame = qof_instance_get_slots( inst );
    slot_info.path = NULL;

    gnc_gda_load_object( be, pModel, row, TABLE_NAME, &slot_info, col_table );

    if( slot_info.path != NULL ) {
        g_string_free( slot_info.path, TRUE );
    }
}

void
gnc_gda_slots_load_for_list( GncGdaBackend* be, GList* list )
{
	QofCollection* coll;
	GdaQuery* query;
	GString* sql;
    gchar guid_buf[GUID_ENCODING_LENGTH+1];
	gboolean first_guid = TRUE;
	GdaDataModel* model;
	gboolean single_item;

	g_return_if_fail( be != NULL );

	// Ignore empty list
	if( list == NULL ) return;

	coll = qof_instance_get_collection( QOF_INSTANCE(list->data) );

	// Create the query for all slots for all items on the list
	sql = g_string_sized_new( 40+(GUID_ENCODING_LENGTH+3)*g_list_length( list ) );
	g_string_append_printf( sql, "SELECT * FROM %s WHERE %s ", TABLE_NAME, obj_guid_col_table[0].col_name );
	if( g_list_length( list ) != 1 ) {
		g_string_append( sql, "IN (" );
		single_item = FALSE;
	} else {
		g_string_append( sql, "= " );
		single_item = TRUE;
	}
	(void)gnc_gda_append_guid_list_to_sql( sql, list, G_MAXUINT );
	if( !single_item ) {
		g_string_append( sql, ")" );
	}

	// Execute the query and load the slots
	query = gnc_gda_create_query_from_sql( be, sql->str );
	model = gnc_gda_execute_select_sql( be, sql->str );
    if( model != NULL ) {
        int numRows = gda_data_model_get_n_rows( model );
        int r;

        for( r = 0; r < numRows; r++ ) {
            load_slot_for_list_item( be, model, r, coll );
        }
		g_object_unref( model );
    }
	g_string_free( sql, TRUE );
}

/* ================================================================= */
static void
create_slots_tables( GncGdaBackend* be )
{
	GError* error = NULL;
	gboolean ok;
	gint version;

	g_return_if_fail( be != NULL );

	version = gnc_gda_get_table_version( be, TABLE_NAME );
	if( version == 0 ) {
    	gnc_gda_create_table( be, TABLE_NAME, TABLE_VERSION, col_table, &error );
		if( error != NULL ) {
			g_critical( "GDA: unable to create SLOTS table: %s\n", error->message );
		}
		ok = gnc_gda_create_index( be, "slots_guid_index", TABLE_NAME, obj_guid_col_table, &error );
		if( !ok ) {
			g_critical( "GDA: unable to create index: %s\n", error->message );
		}
	}
}

/* ================================================================= */
void
gnc_gda_init_slots_handler( void )
{
    static GncGdaDataType_t be_data =
    {
        GNC_GDA_BACKEND_VERSION,
        GNC_ID_ACCOUNT,
        NULL,                    /* commit - cannot occur */
        NULL,                    /* initial_load - cannot occur */
        create_slots_tables        /* create_tables */
    };

    qof_object_register_backend( TABLE_NAME, GNC_GDA_BACKEND, &be_data );
}
/* ========================== END OF FILE ===================== */