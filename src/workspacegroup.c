/* A set of workspaces loaded and saved from a ws file.
 */

/*

    Copyright (C) 1991-2003 The National Gallery

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

 */

/*

    These files are distributed with VIPS - http://www.vips.ecs.soton.ac.uk

 */

/*
#define DEBUG
 */

#include "ip.h"

static FilemodelClass *parent_class = NULL;

Workspace *
workspacegroup_map( Workspacegroup *wsg, workspace_map_fn fn, void *a, void *b )
{
	return( (Workspace *) icontainer_map( ICONTAINER( wsg ), 
		(icontainer_map_fn) fn, a, b ) );
}

static void *
workspacegroup_is_empty_sub( Workspace *ws, gboolean *empty )
{
	if( !workspace_is_empty( ws ) ) {
		*empty = FALSE;
		return( ws );
	}

	return( NULL );
}

gboolean 
workspacegroup_is_empty( Workspacegroup *wsg )
{
	gboolean empty;

	empty = TRUE;
	(void) workspacegroup_map( wsg, 
		(workspace_map_fn) workspacegroup_is_empty_sub, &empty, NULL );

	return( empty );
}

static void 
workspacegroup_dispose( GObject *gobject )
{
	Workspacegroup *wsg;

	g_return_if_fail( gobject != NULL );
	g_return_if_fail( IS_WORKSPACEGROUP( gobject ) );

	wsg = WORKSPACEGROUP( gobject );

#ifdef DEBUG
	printf( "workspacegroup_dispose %s\n", IOBJECT( wsg )->name );
#endif /*DEBUG*/

	IM_FREEF( g_source_remove, wsg->auto_save_timeout );

	icontainer_map( ICONTAINER( wsg ),
		(icontainer_map_fn) icontainer_child_remove, NULL, NULL );

	G_OBJECT_CLASS( parent_class )->dispose( gobject );
}

static gboolean
workspacegroup_top_load( Filemodel *filemodel,
	ModelLoadState *state, Model *parent, xmlNode *xnode )
{
	Workspacegroup *wsg = WORKSPACEGROUP( filemodel );
	Workspaceroot *wsr = WORKSPACEROOT( parent );

	char *new_dir;
	Workspace *ws;
	Column *current_col;
	xmlNode *i, *j, *k;
	char name[FILENAME_MAX];
	int best_major;
	int best_minor;

#ifdef DEBUG
	printf( "workspacegroup_top_load: from %s\n", state->filename );
#endif /*DEBUG*/

	if( strcasecmp( (char *) xnode->name, "Workspace" ) != 0 ||
		!get_sprop( xnode, "filename", name, FILENAME_MAX ) ) {
		error_top( _( "XML load error." ) );
		error_sub( _( "Workspace node missing, or no filename" ) ); 
	}

	/* The top node should be the first workspace. Get the filename this
	 * workspace was saved as so we can work out how to rewrite embedded
	 * filenames.
	 */

	/* The old filename could be non-native, so we must rewrite 
	 * to native form first so g_path_get_dirname() can work.
	 */
	path_compact( name );

	state->old_dir = g_path_get_dirname( name ); 

	new_dir = g_path_get_dirname( state->filename_user );
	path_rewrite_add( state->old_dir, new_dir, FALSE );
	g_free( new_dir );

	/* See commened ot ode in COLUMN load below.
	 */
	printf( "workspace_top_load: compat check on merge is broken!\n" );

	switch( wsg->load_type ) {
	case WORKSPACEGROUP_LOAD_TOP:
		/* Easy ... wsg is a blank Workspacegroup we are loading into. 
		 * No renaming needed, except for the ws, which may need
		 * renaming to be globally unique. 
		 */

		/* Rename to avoid clashes. 
		 */
		if( !get_sprop( xnode, "name", name, FILENAME_MAX ) ) 
			return( FALSE );
		while( compile_lookup( wsr->sym->expr->compile, name ) )
			increment_name( name );
		ws = workspace_new( wsg, name );

		if( get_iprop( xnode, "major", &FILEMODEL( ws )->major ) &&
			get_iprop( xnode, "minor", &FILEMODEL( ws )->minor ) &&
			get_iprop( xnode, "micro", &FILEMODEL( ws )->micro ) )
			FILEMODEL( ws )->versioned = TRUE;

		/* If necessary, load up compatibility definitions.
		 */
		if( !workspace_load_compat( ws, 
			FILEMODEL( ws )->major, FILEMODEL( ws )->minor ) ) 
			return( FALSE );

		if( model_load( MODEL( ws ), state, parent, xnode ) )
			return( FALSE );

		break;

	case WORKSPACE_LOAD_COLUMNS:
		/* Load at column level ... rename columns which clash with 
		 * columns in the current workspace. Also look out for clashes
		 * with columns we will load.
		 */
		for( i = xnode->children; i; i = i->next ) 
			workspace_rename_column_node( ws, 
				state, i, xnode->children );

		/* Load those columns.
		 */
		for( i = xnode->children; i; i = i->next ) 
			if( !model_new_xml( state, MODEL( ws ), i ) )
				return( FALSE );

		/* Is there a version mismatch? Issue a warning.
		if( workspace_have_compat( state->major, state->minor, 
			&best_major, &best_minor ) &&
			(best_major != filemodel->major ||
			best_minor != filemodel->minor) ) {
			error_top( _( "Version mismatch." ) );
			error_sub( _( "File \"%s\" was saved from %s-%d.%d.%d. "
				"You may see compatibility problems." ),
				state->filename, PACKAGE,
				state->major, state->minor, state->micro );
			iwindow_alert( GTK_WIDGET( ws->iwnd ), 
				GTK_MESSAGE_INFO );
		}
		 */

		break;

	case WORKSPACE_LOAD_ROWS:
		current_col = workspace_column_pick( ws );

		/* Rename all rows into current column ... loop over column,
		 * subcolumns, rows.
		 */
		for( i = xnode->children; i; i = i->next ) 
			for( j = i->children; j; j = j->next ) 
				for( k = j->children; k; k = k->next ) 
					workspace_rename_row_node( state, 
						current_col, k );

		/* And load rows.
		 */
		for( i = xnode->children; i; i = i->next ) 
			for( j = i->children; j; j = j->next ) 
				for( k = j->children; k; k = k->next ) 
					if( !model_new_xml( state, 
						MODEL( current_col->scol ), 
						k ) )
						return( FALSE );

		break;

	default:
		g_assert( FALSE );
	}

	return( FILEMODEL_CLASS( parent_class )->top_load( filemodel, 
		state, parent, xnode ) );
}

/* Keep the last WS_RETAIN workspaces as ipfl*.ws files.
 */
#define WS_RETAIN (10)

/* Array of names of workspace files we are keeping.
 */
static char *retain_files[WS_RETAIN] = { NULL };

/* The next one we allocate.
 */
static int retain_next = 0;

/* Save the workspace to one of our temp files.
 */
static gboolean
workspace_checkmark_timeout( Workspace *ws )
{
	ws->auto_save_timeout = 0;

	if( !AUTO_WS_SAVE )
		return( FALSE );

	/* Don't backup auto loaded workspace (eg. preferences). These are
	 * system things and don't need it.
	 */
	if( FILEMODEL( ws )->auto_load )
		return( FALSE );

	/* Do we have a name for this retain file?
	 */
	if( !retain_files[retain_next] ) {
		char filename[FILENAME_MAX];

		/* No name yet - make one up.
		 */
		if( !temp_name( filename, "ws" ) )
			return( FALSE );
		retain_files[retain_next] = im_strdup( NULL, filename );
	}
 
	if( !filemodel_save_all( FILEMODEL( ws ), retain_files[retain_next] ) )
		return( FALSE );

	retain_next = (retain_next + 1) % WS_RETAIN;

	return( FALSE );
}

/* Save the workspace to one of our temp files. Don't save directly (pretty
 * slow), instead set a timeout and save when we're quiet for >1s.
 */
static void
workspace_checkmark( Workspace *ws )
{
	if( !AUTO_WS_SAVE )
		return;
	if( FILEMODEL( ws )->auto_load )
		return;

	IM_FREEF( g_source_remove, ws->auto_save_timeout );
	ws->auto_save_timeout = g_timeout_add( 1000, 
		(GSourceFunc) workspace_checkmark_timeout, ws );
}

/* On safe exit, remove all ws checkmarks.
 */
void
workspace_retain_clean( void )
{
	int i;

	for( i = 0; i < WS_RETAIN; i++ ) {
		if( retain_files[i] ) {
			unlinkf( "%s", retain_files[i] );
			IM_FREE( retain_files[i] );
		}
	}
}

/* Track best-so-far file date here during search.
 */
static time_t date_sofar;

/* This file any better than the previous best candidate? Subfn of below.
 */
static char *
workspace_test_file( char *name, char *name_sofar )
{
	char buf[FILENAME_MAX];
	struct stat st;
	int i;

	im_strncpy( buf, name, FILENAME_MAX );
	path_expand( buf );
	for( i = 0; i < WS_RETAIN; i++ )
		if( retain_files[i] && 
			strcmp( buf, retain_files[i] ) == 0 )
			return( NULL );
	if( stat( buf, &st ) == -1 )
		return( NULL );
#ifdef HAVE_GETEUID
	if( st.st_uid != geteuid() )
		return( NULL );
#endif /*HAVE_GETEUID*/
	if( st.st_size == 0 )
		return( NULL );
	if( date_sofar > 0 && st.st_mtime < date_sofar )
		return( NULL );
	
	strcpy( name_sofar, name );
	date_sofar = st.st_mtime;

	return( NULL );
}

/* Load a workspace, called from a yesno dialog.
 */
static void
workspace_auto_recover_load( iWindow *iwnd, 
	void *client, iWindowNotifyFn nfn, void *sys )
{
	char *filename = (char *) client;
	Mainw *mainw = MAINW( iwindow_get_root_noparent( GTK_WIDGET( iwnd ) ) );
	Workspaceroot *wsr = mainw->wsg->wsr; 

	Workspacegroup *wsg;

	/* Load ws file.
	 */
        progress_begin();
	wsg = mainw_open_workspace( wsr, filename );
	progress_end();

	if( wsg ) {
		/* The filename will be something like
		 * "~/.nip2-7.9.6/tmp/untitled-nip2-0-3904875.ws", very
		 * unhelpful.
		 */
		IM_FREE( FILEMODEL( wsg )->filename );
		iobject_changed( IOBJECT( wsg ) );

		nfn( sys, IWINDOW_YES );
	}
	else
		nfn( sys, IWINDOW_ERROR );
}

/* Do an auto-recover ... search for and load the most recent "ipfl*.ws" file 
 * in the tmp area owned by us, with a size > 0, that's not in our
 * retain_files[] set.
 */
void
workspace_auto_recover( Mainw *mainw )
{
	char *p;
	char *name;
	char buf[FILENAME_MAX];
	char buf2[FILENAME_MAX];

	/* Find the dir we are saving temp files to.
	 */
	if( !temp_name( buf, "ws" ) ) {
		iwindow_alert( GTK_WIDGET( mainw ), GTK_MESSAGE_ERROR );
		return;
	}

	if( (p = strrchr( buf, G_DIR_SEPARATOR )) )
		*p = '\0';

	date_sofar = -1;
	(void) path_map_dir( buf, "*.ws", 
		(path_map_fn) workspace_test_file, buf2 );
	if( date_sofar == -1 ) {
		if( !AUTO_WS_SAVE ) {
			error_top( _( "No backup workspaces found." ) );
			error_sub( "%s", 
				_( "You need to enable \"Auto workspace "
				"save\" in Preferences "
				"before automatic recovery works." ) );
			iwindow_alert( GTK_WIDGET( mainw ), GTK_MESSAGE_INFO );
		}
		else {
			error_top( _( "No backup workspaces found." ) );
			error_sub( _( "No suitable workspace save files found "
				"in \"%s\"" ), buf );
			iwindow_alert( GTK_WIDGET( mainw ), GTK_MESSAGE_INFO );
		}

		return;
	}

	/* Tricksy ... free str with notify callack from yesno.
	 */
	name = im_strdupn( buf2 );

	box_yesno( GTK_WIDGET( mainw ), 
		workspace_auto_recover_load, iwindow_true_cb, name, 
		(iWindowNotifyFn) im_free, name,
		GTK_STOCK_OPEN, 
		_( "Open workspace backup?" ),
		_( "Found workspace \"%s\", dated %s. "
		"Do you want to recover this workspace?" ),
		name, ctime( &date_sofar ) );
}

static void 
workspacegroup_set_modified( Filemodel *filemodel, gboolean modified )
{
	Workspacegroup *wsg = WORKSPACEGROUP( filemodel );

	workspacegroup_checkmark( wsg );

	FILEMODEL_CLASS( parent_class )->set_modified( filemodel, modified );
}

static void
workspacegroup_class_init( WorkspacegroupClass *class )
{
	GObjectClass *gobject_class = (GObjectClass *) class;
	FilemodelClass *filemodel_class = (FilemodelClass *) class;

	parent_class = g_type_class_peek_parent( class );

	/* Create signals.
	 */

	/* Init methods.
	 */
	gobject_class->dispose = workspacegroup_dispose;

	filemodel_class->filetype = filesel_type_workspace;
	filemodel_class->top_load = workspacegroup_top_load;
	filemodel_class->set_modified = workspace_set_modified;
}

static void
workspacegroup_init( Workspacegroup *wsg )
{
}

GType
workspacegroup_get_type( void )
{
	static GType type = 0;

	if( !type ) {
		static const GTypeInfo info = {
			sizeof( WorkspacegroupClass ),
			NULL,           /* base_init */
			NULL,           /* base_finalize */
			(GClassInitFunc) workspacegroup_class_init,
			NULL,           /* class_finalize */
			NULL,           /* class_data */
			sizeof( Workspacegroup ),
			32,             /* n_preallocs */
			(GInstanceInitFunc) workspacegroup_init,
		};

		type = g_type_register_static( TYPE_FILEMODEL, 
			"Workspacegroup", &info, 0 );
	}

	return( type );
}

static void
workspacegroup_link( Workspacegroup *wsg, Workspaceroot *wsr, const char *name )
{
	iobject_set( IOBJECT( wsg ), name, NULL );
	icontainer_child_add( ICONTAINER( wsr ), ICONTAINER( wsg ), -1 );
	wsg->wsr = wsr;

	filemodel_register( FILEMODEL( wsg ) );
}

Workspacegroup *
workspacegroup_new( Workspaceroot *wsr, const char *name )
{
	Workspacegroup *wsg;

#ifdef DEBUG
	printf( "workspacegroup_new: %s\n", name );
#endif /*DEBUG*/

	wsg = WORKSPACEGROUP( g_object_new( TYPE_WORKSPACEGROUP, NULL ) );
	workspacegroup_link( wsg, wsr, name );

	return( wsg );
}

/* Make the blank workspacegroup we present the user with (in the absence of
 * anything else).
 */
Workspacegroup *
workspacegroup_new_blank( Workspaceroot *wsr, const char *name )
{
	Workspacegroup *wsg;

	if( !(wsg = workspacegroup_new( wsr, name )) )
		return( NULL );
	iobject_set( IOBJECT( wsg ), NULL, _( "Default empty workspace" ) );

	return( wsg );
}

Workspacegroup *
workspacegroup_new_filename( Workspaceroot *wsr, const char *filename )
{
	char name[FILENAME_MAX];
	Workspacegroup *wsg;

	name_from_filename( filename, name );
	wsg = workspacegroup_new( wsr, name );
	filemodel_set_filename( FILEMODEL( wsg ), filename );

	return( wsg );
}

static gboolean
workspacegroup_load_empty( Workspacegroup *wsg, Workspaceroot *wsr, 
	const char *filename, const char *filename_user )
{
	printf( "workspacegroup_load_empty:\n" ); 

	return( TRUE );
}

/* Load a file as a workspacegroup.
 */
Workspacegroup *
workspacegroup_new_from_file( Workspaceroot *wsr, 
	const char *filename, const char *filename_user )
{
	Workspacegroup *wsg;

	wsg = WORKSPACEGROUP( g_object_new( TYPE_WORKSPACEGROUP, NULL ) );
	if( !workspacegroup_load_empty( wsg, wsr, filename, filename_user ) ) {
		g_object_unref( G_OBJECT( wsg ) );
		return( NULL );
	}

	return( wsg );
}

/* New workspacegroup from a file.
 */
Workspacegroup *
workspacegroup_new_from_openfile( Workspaceroot *wsr, iOpenFile *of )
{
	Workspacegroup *wsg;

#ifdef DEBUG
	printf( "workspacegroup_new_from_openfile: %s\n", of->fname );
#endif /*DEBUG*/

	wsg = WORKSPACEGROUP( g_object_new( TYPE_WORKSPACEGROUP, NULL ) );
	wsg->load_type = WORKSPACEGROUP_LOAD_TOP;
	if( !filemodel_load_all_openfile( FILEMODEL( wsg ), 
		MODEL( wsr ), of ) ) {
		g_object_unref( G_OBJECT( wsg ) );
		return( NULL );
	}

	filemodel_set_modified( FILEMODEL( wsg ), FALSE );
	filemodel_set_filename( FILEMODEL( wsg ), of->fname );

#ifdef DEBUG
	printf( "(set name = %s)\n", IOBJECT( wsg )->name );
#endif /*DEBUG*/

	return( wsg );
}

Workspacegroup *
workspacegroup_duplicate( Workspacegroup *wsg )
{
	Workspacegroup *new_wsg;

	new_wsg = workspacegroup_new_blank( wsg->wsr, NULL );

	/* Save to file, load back again, see workspace_clone().
	 */
	printf( "workspacegroup_duplicate:\n" );

	return( new_wsg );
}

