#include "vbsp.h"
#include "UtlBuffer.h"
#include "UtlSymbol.h"
#include "UtlRBTree.h"
#include "KeyValues.h"
#include "bsplib.h"
#include "materialpatch.h"
#include "vstdlib/strtools.h"

// case insensitive
static CUtlSymbolTable s_SymbolTable( 0, 32, true );

struct NameTranslationLookup_t
{
	CUtlSymbol m_OriginalFileName;
	CUtlSymbol m_PatchFileName;
};

static bool NameTranslationLessFunc( NameTranslationLookup_t const& src1, 
							  NameTranslationLookup_t const& src2 )
{
	return src1.m_PatchFileName < src2.m_PatchFileName;
}

CUtlRBTree<NameTranslationLookup_t, int> s_MapPatchedMatToOriginalMat( 0, 256, NameTranslationLessFunc );

void AddNewTranslation( const char *pOriginalMaterialName, const char *pNewMaterialName )
{
	NameTranslationLookup_t newEntry;
	
	newEntry.m_OriginalFileName = s_SymbolTable.AddString( pOriginalMaterialName );
	newEntry.m_PatchFileName = s_SymbolTable.AddString( pNewMaterialName );

	s_MapPatchedMatToOriginalMat.Insert( newEntry );
}

const char *GetOriginalMaterialNameForPatchedMaterial( const char *pPatchMaterialName )
{
	const char *pRetName = NULL;
	int id;
	NameTranslationLookup_t lookup;
	lookup.m_PatchFileName = s_SymbolTable.AddString( pPatchMaterialName );
	do
	{
		id = s_MapPatchedMatToOriginalMat.Find( lookup );
		if( id >= 0 )
		{
			NameTranslationLookup_t &found = s_MapPatchedMatToOriginalMat[id];
			lookup.m_PatchFileName = found.m_OriginalFileName;
			pRetName = s_SymbolTable.String( found.m_OriginalFileName );
		}
	} while( id >= 0 );
	if( !pRetName )
	{
		// This isn't a patched material, so just return the original name.
		return pPatchMaterialName;
	}
	return pRetName;
}

void CreateMaterialPatch( const char *pOriginalMaterialName, const char *pNewMaterialName,
						 const char *pNewKey, const char *pNewValue )
{
//	Warning( "CreateMaterialPatch for %s: mat %s, %s = %s\n", pOriginalMaterialName, pNewMaterialName, pNewKey, pNewValue );
	char oldVMTFile[ 512 ];
	char newVMTFile[ 512 ];

	AddNewTranslation( pOriginalMaterialName, pNewMaterialName );
	
	Q_snprintf( oldVMTFile, 512, "materials/%s.vmt", pOriginalMaterialName );
	Q_snprintf( newVMTFile, 512, "materials/%s.vmt", pNewMaterialName );

//	printf( "Creating material patch file %s which points at %s\n", newVMTFile, oldVMTFile );

	KeyValues *kv = new KeyValues( "patch" );
	if ( !kv )
	{
		Error( "Couldn't allocate KeyValues for %s!!!", pNewMaterialName );
	}

	kv->SetString( "include", oldVMTFile );
	KeyValues *section = kv->FindKey( "insert", true );

	section->SetString( pNewKey, pNewValue );

	// Write patched .vmt into a memory buffer
	CUtlBuffer buf( 0, 0, true );
	kv->RecursiveSaveToFile( buf, 0 );

	// Add to pak file for this .bsp
	AddBufferToPack( newVMTFile, (void*)buf.Base(), buf.TellPut(), true );

	// Cleanup
	kv->deleteThis();
}

//-----------------------------------------------------------------------------
// Scan material + all subsections for key
//-----------------------------------------------------------------------------
static bool DoesMaterialHaveKey( KeyValues *pKeyValues, const char *pKeyName )
{
	const char *pVal;
	pVal = pKeyValues->GetString( pKeyName, NULL );
	if ( pVal != NULL  )
		return true;

	for( KeyValues *pSubKey = pKeyValues->GetFirstTrueSubKey(); pSubKey; pSubKey = pSubKey->GetNextTrueSubKey() )
	{
		if ( DoesMaterialHaveKey( pSubKey, pKeyName) )
			return true;
	}
	
	return false;
}

bool DoesMaterialHaveKey( const char *pMaterialName, const char *pKeyName )
{
	char name[512];
	Q_snprintf( name, 512, "materials/%s.vmt", GetOriginalMaterialNameForPatchedMaterial( pMaterialName ) );
	KeyValues *kv = new KeyValues( "blah" );

	if ( !kv->LoadFromFile( g_pFileSystem, name ) )
	{
		kv->deleteThis();
		return NULL;
	}

	bool retVal = DoesMaterialHaveKey( kv, pKeyName );

	kv->deleteThis();
	return retVal;
}

//-----------------------------------------------------------------------------
// Scan material + all subsections for key/value pair
//-----------------------------------------------------------------------------
// VXP: Used in PatchEnvmapForMaterialAndDependents (patching non env_cubemap sides - already done in leak code now by me)
static bool DoesMaterialHaveKeyValuePair( KeyValues *pKeyValues, const char *pKeyName, const char *pSearchValue )
{
	const char *pVal;
	pVal = pKeyValues->GetString( pKeyName, NULL );
	if ( pVal != NULL && ( Q_stricmp( pSearchValue, pVal ) == 0 ) )
		return true;

	for( KeyValues *pSubKey = pKeyValues->GetFirstTrueSubKey(); pSubKey; pSubKey = pSubKey->GetNextTrueSubKey() )
	{
		if ( DoesMaterialHaveKeyValuePair( pSubKey, pKeyName, pSearchValue ) )
			return true;
	}
	
	return false;
}

//-----------------------------------------------------------------------------
// Scan material + all subsections for key/value pair
//-----------------------------------------------------------------------------
bool DoesMaterialHaveKeyValuePair( const char *pMaterialName, const char *pKeyName, const char *pSearchValue )
{
	char name[512];
	Q_snprintf( name, 512, "materials/%s.vmt", GetOriginalMaterialNameForPatchedMaterial( pMaterialName ) );
	KeyValues *kv = new KeyValues( "blah" );

	if ( !kv->LoadFromFile( g_pFileSystem, name ) )
	{
		kv->deleteThis();
		return NULL;
	}

	bool retVal = DoesMaterialHaveKeyValuePair( kv, pKeyName, pSearchValue );

	kv->deleteThis();
	return retVal;
}

bool GetValueFromMaterial( const char *pMaterialName, const char *pKey, char *pValue, int len )
{
	char name[512];
	Q_snprintf( name, 512, "materials/%s.vmt", GetOriginalMaterialNameForPatchedMaterial( pMaterialName ) );
	KeyValues *kv = new KeyValues( "blah" );
	// Load the underlying file so that we can check if env_cubemap is in there.
	if ( !kv->LoadFromFile( g_pFileSystem, name ) )
	{
//		Assert( 0 );
		kv->deleteThis();
		return NULL;
	}

	const char *pTmpValue = kv->GetString( pKey, NULL );
	
	if( pTmpValue )
	{
		Q_strncpy( pValue, pTmpValue, len );
	}

	kv->deleteThis();
	return ( pTmpValue != NULL );
}

MaterialSystemMaterial_t FindOriginalMaterial( const char *materialName, bool *pFound, bool bComplain )
{
	MaterialSystemMaterial_t matID;
	matID = FindMaterial( GetOriginalMaterialNameForPatchedMaterial( materialName ), pFound, bComplain );
	return matID;
}

