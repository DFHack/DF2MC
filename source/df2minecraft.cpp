/*
DF2MC version 0.7
Dwarf Fortress To Minecraft
Converts Dwarf Frotress Game Maps into Minecraft Game Level for use as a
Dwarf Fortress 3D visulaizer or for creating Minecraft levels to play in.

Copyright (c) 2010 Brian Risinger (TroZ)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.


A copy of the license is also available at:
http://www.gnu.org/licenses/old-licenses/gpl-2.0.html


This source code has a project at:
http://github.com/TroZ/DF2MC

*/
#include <iostream>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <hash_set>
#include <stdio.h>
#include <algorithm>
#include <time.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "Console.h"
#include "Core.h"
#include "Export.h"
#include "PluginManager.h"
#include <TileTypes.h>
#include <tinyxml.h>

#include <zlib.h>
#include <modules/Maps.h>
#include <modules/Materials.h>
#include <modules/Constructions.h>
#include <modules/Buildings.h>
#include <VersionInfo.h>
#include <modules/Gui.h>
#include "df/tiletype.h"
#include <df/creature_raw.h>


#ifdef LINUX_BUILD
    // pull hash_* into std
    namespace std
    {
    using namespace __gnu_cxx;
    }
    // no O_BINARY or O_TEXT on decent operating systems
    #define O_BINARY 0
    #define _O_CREAT O_CREAT
    #define _O_WRONLY O_WRONLY
    #define _O_TRUNC O_TRUNC
    #define _S_IREAD S_IREAD
    #define _S_IWRITE S_IWRITE
#endif

#if _MSC_VER
    #define snprintf _snprintf
    #include <io.h>
    #include <direct.h>
#endif
#ifdef LINUX_BUILD
    #include <strings.h>
    #define stricmp strcasecmp
    #define strnicmp strncasecmp
    #define _atoi64 atoll
    int make_dir (const char * path, int mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)
    {
        return mkdir(path, mode);
    }
    char* strrev(char* szT)
    {
        if ( !szT )
            return "";
        int i = strlen(szT);
        int t = !(i%2)? 1 : 0;
        for(int j = i-1 , k = 0 ; j > (i/2 -t) ; j-- )
        {
            char ch  = szT[j];
            szT[j]   = szT[k];
            szT[k++] = ch;
        }
        return szT;
    }
#else
    int make_dir (const char * path, int mode = 0)
    {
        return mkdir(path);
    }
#endif

using namespace std;
using namespace DFHack;
using df::global::world;
using namespace df::enums;

#define CHUNK (1024*256)

static uint32_t SQUARESPERBLOCK = 16;//number of squares per DF block

std::map<std::string,uint8_t> mcMats;       //Minecraft material name to id
//std::map<int,int> dfMat2mcMat;            //DF Material name to minecraft id
std::map<std::string,uint8_t*> dfMats;  //what a wall of a particular df material looks like
std::map<std::string,uint8_t*> terrain; //object description string to minecraft material array of size 2 * squaresize * squaresize * squaresize; 0 is material, 1 is data
std::map<std::string,uint8_t*> plants;  //object description string to minecraft material array of size 2 * squaresize * squaresize * squaresize; 0 is material, 1 is data
std::map<std::string,uint8_t*> buildings;   //object description string to minecraft material array of size 2 * squaresize * squaresize * squaresize; 0 is material, 1 is data
std::map<std::string,uint8_t*> flows;   //object description string to minecraft material array of size 2 * squaresize * squaresize * squaresize; 0 is material, 1 is data
std::map<std::string,std::string> buildingNeighbors; //the buildings (second) to align a build (first) to face
std::hash_set<int> sandhash;                //list of objects that respond to gravity (sand)
std::hash_set<int> supporthash;             //list of objects that don't support thing that respond to gravity (water, lava, etc)

int cubeSkyOpacity[256];//how much light each block absorbes from the sky
int cubeBlockOpacity[256];//how much light each block absorbes from the block sources
int cubePartialLit[256];//if the block is partially light - mostly 1/2 step and stais blocks, that are lit themselves but block light from passing through

#define CHUNK_HEIGHT 128

TiXmlElement *settings;
TiXmlElement *materialmapping;
//TiXmlElement *xmlobjects;
TiXmlElement *xmlmaterials;
TiXmlElement *xmlterrain;
TiXmlElement *xmlplants;
TiXmlElement *xmlbuildings;
TiXmlElement *xmlflows;

// FIXME: really SHOULD be obsolete
const char TileClassNames[][17] =
{
    "empty",
    "floor"
    "boulder",
    "pebbles",
    "wall",
    "forticication",
    "stairup",
    "stairdown",
    "stairupdown",
    "ramp",
    "ramptop",
    "brook_bed",
    "river_top",
    "tree",
    "sapling",
    "shrub",
    "endless_pit"
};

const char TileMaterialNames[][21]=
{
    "air",
    "soil",
    "stone",
    "featstone",
    "obsidian",
    "vein",
    "ice",
    "constructed",
    "grass",
    "grass2",
    "grassdry",
    "grassdead",
    "hfs",
    "campfire",
    "fire",
    "ashes",
    "magma",
    "driftwood",
    "pool",
    "brook",
    "river"
};

// FIXME: move to df-structures
const char* Months[13][16]=
{
    "Granite",
    "Slate"
    "Felsite",
    "Hematite",
    "Malachite",
    "Galena",
    "Limestone",
    "Sandstone",
    "Timber",
    "Moonstone"
    "Opal",
    "Obsidian",
    "INVALID"
};


struct myBuilding
{
    char type[256];
    char desc[16];
    DFHack::t_matglossPair material;
};

struct myConstruction
{
    uint16_t form; // e_construction_base
    uint16_t mat_type;
    uint32_t mat_idx;
};


char *airarray=NULL;
char *intarray=NULL;

bool createUnknown = true;

#define LIMITNONE   0
#define LIMITTOP    1
#define LIMITRANGE  2
#define LIMITSMART  3

//settings
int squaresize=3;//number of minecraft blocks per DF square
int torchPerInside = 10;
int torchPerDark = 20;
int torchPerSubter = 30;
int directionalWalls = 0;
int safesand = 3;

uint32_t limitxmin = 0;
uint32_t limitxmax = 1000;
uint32_t limitymin = 0;
uint32_t limitymax = 1000;
uint32_t limittype = LIMITNONE;
uint32_t limitlevels = 1000;
uint32_t limitairtokeep = 1000;
uint32_t limittoplevel = 0;
uint8_t limitz[1000];

int64_t seed = 0x004446746F4D43FF;// DFtoMC

int biome = 0; //for now = 0 is normal, 1 is snow
int snowy = 0;

//stats counters
#define UNKNOWN     0
#define IMPERFECT   1
#define PERFECT     2
#define UNSEEN      3
#define STAT_TYPES  4
#define STAT_AREAS  5
#define MATERIALS   0
#define TERRAIN     1
#define FLOWS       2
#define PLANTS      3
#define BUILDINGS   4
int stats[STAT_AREAS][STAT_TYPES];

void loadMcMats ( TiXmlDocument* doc, color_ostream & out )
{

    out.print ( "Loading Minecraft Materials...\n" );

    char* tagname = "minecraftmaterialsalpha";
    TiXmlElement *elm = doc->FirstChildElement();
    while ( elm!=NULL )
    {

        if ( strcmp ( elm->Value(),tagname ) ==0 )
        {
            TiXmlElement *mat = elm->FirstChildElement();
            while ( mat!=NULL )
            {

                if ( strcmp ( mat->Value(),"material" ) ==0 )
                {
                    int val;
                    mat->Attribute ( "val",&val );
                    string name ( mat->GetText() );

                    //DFConsole->print("%s -> %d\n",name.c_str(),val);
                    if ( val>=0 && val <256 )
                    {
                        mcMats[name]= ( uint8_t ) val;

                        int opacity;
                        if ( mat->Attribute ( "opacity",&opacity ) !=NULL )
                        {
                            cubeSkyOpacity[val] = max ( min ( opacity,15 ),0 );
                            cubeBlockOpacity[val] = max ( min ( opacity,15 ),-15 );
                        }

                        if ( mat->Attribute ( "skyopacity",&opacity ) !=NULL )
                        {
                            cubeSkyOpacity[val] = max ( min ( opacity,15 ),0 );
                        }

                        if ( mat->Attribute ( "blockopacity",&opacity ) !=NULL )
                        {
                            cubeBlockOpacity[val] = max ( min ( opacity,15 ),-15 );
                        }

                        if ( mat->Attribute ( "partlit",&opacity ) !=NULL )
                        {
                            cubePartialLit[val] = opacity;
                        }

                        if ( mat->Attribute ( "sand",&opacity ) !=NULL && opacity==1 )
                        {
                            sandhash.insert ( val );
                        }

                        if ( mat->Attribute ( "nonsupport",&opacity ) !=NULL && opacity==1 )
                        {
                            supporthash.insert ( val );
                        }
                    }
                    mat=mat->NextSiblingElement();
                }
            }
        }

        elm=elm->NextSiblingElement();
    }

    out.print ( "loaded %d materials\n\n",mcMats.size() );

}

char* makeAirArray()
{
    //makes an array representing an empty block for the current squaresize setting
    if ( airarray==NULL )
    {
        airarray=new char[1024*10];
        char *pos=airarray;
        for ( int z=0;z<squaresize;z++ )
        {
            if ( z>0 )
            {
                *pos='|';
                pos++;
            }
            for ( int y=0;y<squaresize;y++ )
            {
                if ( y>0 )
                {
                    *pos=';';
                    pos++;
                }
                for ( int x=0;x<squaresize;x++ )
                {
                    if ( x>0 )
                    {
                        *pos=',';
                        pos++;
                    }
                    *pos='a';
                    pos++;
                    *pos='i';
                    pos++;
                    *pos='r';
                    pos++;
                }
            }
        }
        *pos='\0';
    }
    return airarray;
}

char* makeZeroArray()
{
    //makes an array representing an empty block for the current squaresize setting
    if ( intarray==NULL )
    {
        intarray=new char[1024*10];
        char *pos=intarray;
        for ( int z=0;z<squaresize;z++ )
        {
            if ( z>0 )
            {
                *pos='|';
                pos++;
            }
            for ( int y=0;y<squaresize;y++ )
            {
                if ( y>0 )
                {
                    *pos=';';
                    pos++;
                }
                for ( int x=0;x<squaresize;x++ )
                {
                    if ( x>0 )
                    {
                        *pos=',';
                        pos++;
                    }
                    *pos='0';
                    pos++;
                }
            }
        }
        *pos='\0';
    }
    return intarray;
}


uint8_t* makeAirArrayInt()
{

    uint8_t *data = new uint8_t[2*squaresize*squaresize*squaresize];
    memset ( data,0,sizeof ( int8_t ) *2*squaresize*squaresize*squaresize );

    return data;
}

std::vector<std::string> split ( const std::string &s, const char *delim, std::vector<std::string> &elems )
{
    //std::stringstream ss(s);
    //std::string item;
    //while(std::getline(ss, item, *delim)) {
    //    elems.push_back(item);
    //}

    unsigned int start = 0;
    unsigned int end = 0;
    while ( end<s.length() )
    {
        while ( strchr ( delim,s.c_str() [end] ) ==NULL && end<s.length() )
        {
            end++;
        }
        elems.push_back ( s.substr ( start,end-start ) );
        start=end+1;
        end=start;
    }

    return elems;
}


void loadObject (DFHack::color_ostream & c, TiXmlElement *elm, std::map<std::string,uint8_t*> &objects, bool allowFace = false )
{

    //iterate through elements adding them into the objects map
    while ( elm!=NULL )
    {
        //bool test=false;

        const char *val = elm->Attribute ( "mat" );
        if ( val!=NULL )
        {
            const char* name = elm->Value();
            string data = "";
            string face = "";
            string val = elm->Attribute ( "mat" );
            if ( elm->Attribute ( "data" ) !=NULL )
            {
                data = elm->Attribute ( "data" );
            }
            if ( elm->Attribute ( "face" ) !=NULL )
            {
                face = elm->Attribute ( "face" );
            }


            int size = squaresize*squaresize*squaresize;
            uint8_t *obj = new uint8_t[2*size];
            memset ( obj,0,2*size*sizeof ( uint8_t ) );

            //parse val
            std::vector<std::string> vals;
            split ( val,",;|",vals );
            if ( vals.size() != ( squaresize*squaresize*squaresize ) )
            {
                c.print ( "Object %s doesn't have correct number of materials\n (%d expeted %d) - resetting to Air\n",name,vals.size(), ( squaresize*squaresize*squaresize ) );
                vals.clear();
                val = makeAirArray();
                elm->SetAttribute ( "mat",val.c_str() );
                split ( val,",;|",vals );
            }

            for ( unsigned int i=0;i<vals.size();i++ )
            {
                std::map<std::string,uint8_t>::iterator it;
                it = mcMats.find ( vals[i] );
                if ( it!=mcMats.end() )
                {
                    obj[i]=it->second;
                }
                else
                {
                    int num = atoi ( vals[i].c_str() );
                    if ( num>=-1 && num<256 )
                    {
                        obj[i]=num;
                    }
                    else
                    {
                        if ( vals[i].c_str() [0]!='0' )
                        {
                            c.print ( "Unknown Minecraft Material %s in object %s - using Air\n",vals[i].c_str(),name );
                        }
                        obj[i]=0;
                    }
                }
            }

            //parse data
            if ( data.length() >0 )
            {
                std::vector<std::string> datas;
                split ( data,",;|",datas );
                if ( datas.size() != ( squaresize*squaresize*squaresize ) )
                {
                    c.print ( "Object %s doesn't have correct number of materials\n (%d expeted %d) - resetting to 0",name,datas.size(), ( squaresize*squaresize*squaresize ) );
                    datas.clear();
                    data = makeZeroArray();
                    elm->SetAttribute ( "data",data.c_str() );
                    split ( data,",;|",vals );
                }

                for ( unsigned int i=0;i<datas.size();i++ )
                {
                    int8_t d=0;
                    d = atoi ( datas[i].c_str() );
                    //if(d!=0){
                    //  DFConsole->print("non-zero d");
                    //  test=true;
                    //}
                    obj[i + ( squaresize*squaresize*squaresize ) ] = ( d&0xf ) <<4;//data it the upper nibble, lighting appears to be the lower nibble.
                }
            }

            //ok, now add to objects map
            if ( objects.find ( name ) !=objects.end() )
            {
                c.print ( "\t %s is already defined - overwritting\n",name );
            }
            objects[name] = obj;

            //if(test){
            //  uint8_t *thing = objects.find(name)->second;
            //  DFConsole->print("%s\n",name);
            //  for(int i=0;i<(size);i++){
            //      DFConsole->print("%d-\t%d\t%d\n",i,thing[i],thing[i+size]);
            //  }
            //}

            if ( allowFace && face.length() >0 )
            {
                buildingNeighbors[name]=face;
            }

        }

        elm = elm->NextSiblingElement();
    }

}

void loadDFObjects(DFHack::color_ostream & c)
{

    c.print ( "Loading DF to MC Object Definations...\n" );

    TiXmlElement *elm = xmlmaterials->FirstChildElement();
    loadObject ( c, elm,dfMats );
    c.print ( "loaded %d DF materials\n",dfMats.size() );

    elm = xmlterrain->FirstChildElement();
    loadObject ( c, elm,terrain, true );
    c.print ( "loaded %d terrain types\n",terrain.size() );

    elm = xmlflows->FirstChildElement();
    loadObject ( c, elm,flows );
    c.print ( "loaded %d flow types\n",flows.size() );

    elm = xmlplants->FirstChildElement();
    loadObject ( c, elm,plants );
    c.print ( "loaded %d plant types\n",plants.size() );

    elm = xmlbuildings->FirstChildElement();
    loadObject ( c, elm,buildings, true );
    c.print ( "loaded %d building types\n\n",buildings.size() );

}

int compressFile (DFHack::color_ostream & console, char* src, char* dest )
{
    //compress file gzip
    int ret, flush;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];

    FILE *s = fopen ( "out.mcraw","rb" );
    if ( s==NULL )
    {
        console.printerr ( "Could not open file for reading, exiting." );
        return -50;
    }

    FILE *f = fopen ( dest,"wb" );
    if ( f==NULL )
    {
        console.printerr ( "Could not open file for writing, exiting." );
        return -51;
    }

    //most of the rest of this is from a zlib example

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit2 ( &strm, Z_BEST_COMPRESSION,Z_DEFLATED,31,9,Z_DEFAULT_STRATEGY );//the 31 indicates gzip, set to 15 for normal zlib file header
    if ( ret != Z_OK )
    {
        console.printerr ( "Unable to initalize compression routine, exiting." );
        return ret;
    }

    /* compress until end of file */
    do
    {
        strm.avail_in = fread ( in, 1, CHUNK, s );
        if ( ferror ( s ) )
        {
            ( void ) deflateEnd ( &strm );
            return Z_ERRNO;
        }
        flush = feof ( s ) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;

        /* run deflate() on input until output buffer not full, finish
        compression if all of source has been read in */
        do
        {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = deflate ( &strm, flush ); /* no bad return value */
            assert ( ret != Z_STREAM_ERROR );  /* state not clobbered */
            have = CHUNK - strm.avail_out;
            if ( fwrite ( out, 1, have, f ) != have || ferror ( f ) )
            {
                ( void ) deflateEnd ( &strm );
                return Z_ERRNO;
            }
        }
        while ( strm.avail_out == 0 );
        assert ( strm.avail_in == 0 );  /* all input will be used */

        /* done when last data in file processed */
    }
    while ( flush != Z_FINISH );
    assert ( ret == Z_STREAM_END );     /* stream will be complete */


    /* clean up and return */
    ( void ) deflateEnd ( &strm );

    fclose ( f );
    fclose ( s );

    return Z_OK;
}

void base36 ( int val, char* str )
{
    char *alpha = "0123456789abcdefghijklmnopqrstuvwxyz";
    char *ptr=str;
    bool neg = false;
    if ( val<0 )
    {
        neg=true;
        val*=-1;
    }
    if ( val==0 )
    {
        *ptr='0';
        ptr++;
    }
    while ( val!=0 )
    {
        *ptr=alpha[val%36];
        ptr++;
        val=val/36;
    }
    if ( neg )
    {
        *ptr='-';
        ptr++;
    }
    *ptr='\0';
    strrev ( str );
}

int saveChunk ( DFHack::color_ostream & out, char* dirname,uint8_t* mclayers,uint8_t* mcdata,uint8_t* mcskylight,uint8_t* mcblocklight,int mcxsquares,int mcysquares,int mczsquares,int x, int y, int64_t &size )
{

    //make sure path for chunk exists - this should be x then y but south is +x and east is -y
    char path[1024];
    char part[16];
    //int xpos = y/16;
    //int ypos = -x/16;
    int xpos = x/16;
    int ypos = y/16;
    base36 ( xpos%64,part );
    snprintf ( path,1023,"%s/%s",dirname,part );
    path[1023]='\0';
    if ( access ( path,0 ) !=0 )
    {
        if ( make_dir ( path ) !=0 )
            return -100;
    }
    int mod = ypos%64;
    if ( mod<0 ) mod+=64;//the plus 64 is because mod of a negative number is a negative, but we want 0-63
    base36 ( mod,part );
    strncat ( path,"/",1023 );
    strncat ( path,part,1023 );
    path[1023]='\0';
    if ( access ( path,0 ) !=0 )
    {
        if ( make_dir ( path ) !=0 )
            return -101;
    }

    //make file name
    strncat ( path,"/c.",1023 );
    base36 ( xpos,part );
    strncat ( path,part,1023 );
    strncat ( path,".",1023 );
    base36 ( ypos,part );
    strncat ( path,part,1023 );
    strncat ( path,".dat",1023 );
    path[1023]='\0';


    int of = open ( "out.mcraw", O_BINARY | _O_CREAT | _O_TRUNC | _O_WRONLY,_S_IREAD | _S_IWRITE );

    //first write the raw file to disk
    write ( of,"\012\000\000\012\000\005Level\011\000\010Entities\012\000\000\000\000\011\000\014TileEntities\012\000\000\000\000\001\000\020TerrainPopulated\001\004\000\012LastUpdate",80 );
    int64_t timer = 0;//apparently game time, not unix time
    //_time64(&timer);
    write ( of, ( ( char* ) ( &timer ) ) +7,1 );
    write ( of, ( ( char* ) ( &timer ) ) +6,1 );
    write ( of, ( ( char* ) ( &timer ) ) +5,1 );
    write ( of, ( ( char* ) ( &timer ) ) +4,1 );
    write ( of, ( ( char* ) ( &timer ) ) +3,1 );
    write ( of, ( ( char* ) ( &timer ) ) +2,1 );
    write ( of, ( ( char* ) ( &timer ) ) +1,1 );
    write ( of, ( ( char* ) ( &timer ) ) +0,1 );

    write ( of,"\003\000\004xPos",7 );
    char val[9];
    val[0] = * ( ( ( char* ) ( &xpos ) ) +3 );
    val[1] = * ( ( ( char* ) ( &xpos ) ) +2 );
    val[2] = * ( ( ( char* ) ( &xpos ) ) +1 );
    val[3] = * ( ( ( char* ) ( &xpos ) ) +0 );
    val[4] = '\0';
    write ( of,val,4 );
    write ( of,"\003\000\004zPos",7 );
    val[0] = * ( ( ( char* ) ( &ypos ) ) +3 );
    val[1] = * ( ( ( char* ) ( &ypos ) ) +2 );
    val[2] = * ( ( ( char* ) ( &ypos ) ) +1 );
    val[3] = * ( ( ( char* ) ( &ypos ) ) +0 );
    val[4] = '\0';
    write ( of,val,4 );


    //prepair data
    char blocks[16*16*CHUNK_HEIGHT];
    char data[16*16*CHUNK_HEIGHT/2];
    char skylight[16*16*CHUNK_HEIGHT/2];
    char blocklight[16*16*CHUNK_HEIGHT/2];
    char heightmap[16*16];
    memset ( blocks,0,16*16*CHUNK_HEIGHT );
    memset ( data,0,16*16*CHUNK_HEIGHT/2 );
    memset ( skylight,0,16*16*CHUNK_HEIGHT/2 );
    memset ( blocklight,0,16*16*CHUNK_HEIGHT/2 );
    memset ( heightmap,0,16*16 );

    for ( int zz=0;zz<mczsquares;zz++ )
    {
        for ( int yy=0;yy<16;yy++ )
        {
            for ( int xx=0;xx<16;xx++ )
            {

                //get info for current position
                int idx = x+xx + ( zz * mcysquares +y+yy ) * mcxsquares;
                assert ( idx< ( mcxsquares*mcysquares*mczsquares ) );

                int blocktype = mclayers[idx];
                int blockdata = mcdata[idx]; //upper nibble is the data because of mclevel format
                int skyl = mcskylight[idx];
                int blockl = mcblocklight[idx];

                int index = zz + ( yy * CHUNK_HEIGHT + ( xx * CHUNK_HEIGHT * 16 ) ) ;
                assert ( index< ( 16 * 16 * CHUNK_HEIGHT ) );

                blocks[index] = blocktype;

                if ( blocktype!=0 )
                    heightmap[yy*16 + xx] = min ( zz+1,CHUNK_HEIGHT-1 );

                if ( zz%2==0 )
                {
                    data[index/2] |= ( blockdata >> 4 );
                    skylight[index/2] |= ( skyl & 0x0f );
                    blocklight[index/2] |= ( blockl &0x0f );
                }
                else
                {
                    data[index/2] |= ( blockdata & 0xf0 );
                    skylight[index/2] |= ( ( skyl & 0x0f ) << 4 );
                    blocklight[index/2] |= ( ( blockl &0x0f ) << 4 );
                }
            }
        }
    }

    //blocks
    write ( of,"\007\000\006Blocks",9 );
    int isize = 16 * 16 * CHUNK_HEIGHT;
    val[0] = * ( ( ( char* ) ( &isize ) ) +3 );
    val[1] = * ( ( ( char* ) ( &isize ) ) +2 );
    val[2] = * ( ( ( char* ) ( &isize ) ) +1 );
    val[3] = * ( ( ( char* ) ( &isize ) ) +0 );
    val[4] = '\0';
    write ( of,val,4 );
    write ( of, ( char* ) blocks,isize );
    //data
    write ( of,"\007\000\004Data",7 );
    isize = 16 * 16 * CHUNK_HEIGHT/2;
    val[0] = * ( ( ( char* ) ( &isize ) ) +3 );
    val[1] = * ( ( ( char* ) ( &isize ) ) +2 );
    val[2] = * ( ( ( char* ) ( &isize ) ) +1 );
    val[3] = * ( ( ( char* ) ( &isize ) ) +0 );
    val[4] = '\0';
    write ( of,val,4 );
    write ( of, ( char* ) data,isize );
    //skylight
    write ( of,"\007\000\010SkyLight",11 );
    write ( of,val,4 );//same size as above
    write ( of, ( char* ) skylight,isize );
    //blocklight
    write ( of,"\007\000\012BlockLight",13 );
    write ( of,val,4 );//same size as above
    write ( of, ( char* ) blocklight,isize );
    //heightMap
    write ( of,"\007\000\011HeightMap",12 );
    isize = 16 * 16;
    val[0] = * ( ( ( char* ) ( &isize ) ) +3 );
    val[1] = * ( ( ( char* ) ( &isize ) ) +2 );
    val[2] = * ( ( ( char* ) ( &isize ) ) +1 );
    val[3] = * ( ( ( char* ) ( &isize ) ) +0 );
    val[4] = '\0';
    write ( of,val,4 );
    write ( of, ( char* ) heightmap,isize );


    //now close tag_compound 'level'
    write ( of,"\000",1 );

    /*  write(of,"\010\000\013GeneratedBy",14);
    char str[256];
    strcpy(str,"Dwarf Fortress To Minecraft by TroZ");
    int len = strlen(str);
    write(of,((char*)&(len))+1,1);
    write(of,&(len),1);
    write(of,str,len);
    */
    write ( of,"\000",1 );// end of unnamed compound

    close ( of );


    int res = compressFile (out,"out.mcraw",path );
    if ( res != Z_OK )
    {
        out.printerr ( "\nError compressing file (%d)\n",res );
    }
    else
    {

        //now get file size
        struct stat filestatus;
        if ( stat ( path,&filestatus ) ==0 )
        {
            size = filestatus.st_size;
        }
        else
        {
            out.printerr ( " Error getting file size " );
        }
    }

    return res;
}

int saveMCLevelAlpha ( DFHack::color_ostream & out, uint8_t* mclayers,uint8_t* mcdata,uint8_t* mcskylight,uint8_t* mcblocklight,int mcxsquares,int mcysquares,int mczsquares, int xs, int ys, int zs,char* name )
{

    out.print ( "\n\nSaving...\n" );

    zs++;//I think this is needed - still need to test

    char dirname[256];
    char tempname[250];
    if ( name==NULL || strlen ( name ) <1 )
    {
        snprintf ( tempname,250,"World " );
    }
    else
    {
        strncpy ( tempname,name,250 );
        tempname[249]='\0';
    }
    //find not use dir name
    int count=1;
    do
    {
        snprintf ( dirname,255,"%s%d",tempname,count );
        count++;
    }
    while ( access ( dirname, 0 ) ==0 && count < 10000 );

    if ( count>9999 )
        return count;

    if ( make_dir ( dirname ) !=0 )
    {
        return -1;
    }

    int64_t totalsize = 0;
    int64_t size = 0;

    //ok iterate through 16x16 blocks and save individual chunck files off
    for ( int x=0;x<mcxsquares;x+=16 )
    {
        out.print ( "." );
        for ( int y=0;y<mcysquares;y+=16 )
        {
            size = 0;
            int ret = saveChunk ( out, dirname, mclayers, mcdata, mcskylight, mcblocklight, mcxsquares, mcysquares, mczsquares, x, y, size );
            if ( ret != 0 )
            {
                out.print ( "Error writing file!\n" );
                return ret;
            }
            else
            {
                totalsize += size;
            }
        }
    }


    //now save the main index.dat
    int of = open ( "out.mcraw",O_BINARY | _O_CREAT | _O_TRUNC | _O_WRONLY,_S_IREAD | _S_IWRITE );

    //first write the raw file to disk
    write ( of,"\012\000\000\012\000\004Data\001\000\013SnowCovered",24 );
    if ( snowy==0 )
        write ( of, ( ( char* ) ( &biome ) ),1 );
    else if ( snowy>0 )
    {
        write ( of, ( ( char* ) 1 ),1 );
    }
    else
    {
        write ( of, ( ( char* ) 0 ),1 );
    }
    write ( of,"\004\000\012LastPlayed",13 );
    int64_t timer = 0;
    // FIXME: not 64-bit on linux. Will fail in year 2038. I think this is not urgent :P
    timer = time(NULL);
    timer *= 1000; //java stores time in milliseconds
    write ( of, ( ( char* ) ( &timer ) ) +7,1 );
    write ( of, ( ( char* ) ( &timer ) ) +6,1 );
    write ( of, ( ( char* ) ( &timer ) ) +5,1 );
    write ( of, ( ( char* ) ( &timer ) ) +4,1 );
    write ( of, ( ( char* ) ( &timer ) ) +3,1 );
    write ( of, ( ( char* ) ( &timer ) ) +2,1 );
    write ( of, ( ( char* ) ( &timer ) ) +1,1 );
    write ( of, ( ( char* ) ( &timer ) ) +0,1 );

    write ( of,"\004\000\004Time\000\000\000\000\000\000\000\000\004\000\012RandomSeed",28 );

    char val[9];
    val[0] = * ( ( ( char* ) ( &seed ) ) +7 );
    val[1] = * ( ( ( char* ) ( &seed ) ) +6 );
    val[2] = * ( ( ( char* ) ( &seed ) ) +5 );
    val[3] = * ( ( ( char* ) ( &seed ) ) +4 );
    val[4] = * ( ( ( char* ) ( &seed ) ) +3 );
    val[5] = * ( ( ( char* ) ( &seed ) ) +2 );
    val[6] = * ( ( ( char* ) ( &seed ) ) +1 );
    val[7] = * ( ( ( char* ) ( &seed ) ) +0 );
    val[8] = '\0';
    write ( of,val,8 );

    write ( of,"\012\000\006Player\002\000\006Health\000\024\001\000\010OnGround\001\002\000\003Air\001\000\002\000\004Fire\xff\xec\002\000\010HurtTime\000\000\002\000\011DeathTime\000\000\002\000\012AttackTime\000\000\003\000\005Score\000\000\000\000\005\000\014FallDistance\000\000\000\000\011\000\011Inventory\012",135 );
    short entid[] = {276,277,278,279,293,261,345,280,263,282,262,310,311,312,313};
    char entcnt[] = {1, 1,  1,  1,  1,  1,  1,  64, 64, 64, 64, 1,  1,  1,  1};
    char entpos[] = {1, 2,  3,  4,  5,  6,  8,  18, 19, 20, 21, 27, 28, 29, 30};
    int numinv = 15;
    val[0] = * ( ( ( char* ) ( &numinv ) ) +3 );
    val[1] = * ( ( ( char* ) ( &numinv ) ) +2 );
    val[2] = * ( ( ( char* ) ( &numinv ) ) +1 );
    val[3] = * ( ( ( char* ) ( &numinv ) ) +0 );
    val[4] = '\0';
    write ( of,val,4 );
    for ( int i=0;i<numinv;i++ )
    {
        write ( of,"\002\000\002id",5 );
        write ( of, ( ( char* ) & ( entid[i] ) ) +1,1 );
        write ( of,& ( entid[i] ),1 );
        write ( of,"\001\000\005Count",8 );
        write ( of,&entcnt[i],1 );
        write ( of,"\001\000\004Slot",7 );
        write ( of,&entpos[i],1 );
        write ( of,"\002\000\005Damage\000\000",10 );
        write ( of,"\000",1 );
    }
    write ( of,"\011\000\003Pos\006\000\000\000\003",11 );
    double xf = ( double ) xs + 0.5;
    double yf = ( double ) ys + 0.5;
    double zf = ( double ) zs + 0.05;
    //float negxf = -xf;
    val[0] = * ( ( ( char* ) & ( xf ) ) +7 );
    val[1] = * ( ( ( char* ) & ( xf ) ) +6 );
    val[2] = * ( ( ( char* ) & ( xf ) ) +5 );
    val[3] = * ( ( ( char* ) & ( xf ) ) +4 );
    val[4] = * ( ( ( char* ) & ( xf ) ) +3 );
    val[5] = * ( ( ( char* ) & ( xf ) ) +2 );
    val[6] = * ( ( ( char* ) & ( xf ) ) +1 );
    val[7] = * ( ( ( char* ) & ( xf ) ) +0 );
    write ( of,val,8 );
    val[0] = * ( ( ( char* ) & ( zf ) ) +7 );
    val[1] = * ( ( ( char* ) & ( zf ) ) +6 );
    val[2] = * ( ( ( char* ) & ( zf ) ) +5 );
    val[3] = * ( ( ( char* ) & ( zf ) ) +4 );
    val[4] = * ( ( ( char* ) & ( zf ) ) +3 );
    val[5] = * ( ( ( char* ) & ( zf ) ) +2 );
    val[6] = * ( ( ( char* ) & ( zf ) ) +1 );
    val[7] = * ( ( ( char* ) & ( zf ) ) +0 );
    write ( of,val,8 );
    val[0] = * ( ( ( char* ) & ( yf ) ) +7 );
    val[1] = * ( ( ( char* ) & ( yf ) ) +6 );
    val[2] = * ( ( ( char* ) & ( yf ) ) +5 );
    val[3] = * ( ( ( char* ) & ( yf ) ) +4 );
    val[4] = * ( ( ( char* ) & ( yf ) ) +3 );
    val[5] = * ( ( ( char* ) & ( yf ) ) +2 );
    val[6] = * ( ( ( char* ) & ( yf ) ) +1 );
    val[7] = * ( ( ( char* ) & ( yf ) ) +0 );
    write ( of,val,8 );
    write ( of,"\011\000\010Rotation\005\000\000\000\002\000\000\000\000\000\000\000\000",24 );
    write ( of,"\011\000\006Motion\006\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000",38 );
    write ( of,"\000",1 );

    write ( of,"\003\000\006SpawnX",9 );
    val[0] = * ( ( ( char* ) ( &xs ) ) +3 );
    val[1] = * ( ( ( char* ) ( &xs ) ) +2 );
    val[2] = * ( ( ( char* ) ( &xs ) ) +1 );
    val[3] = * ( ( ( char* ) ( &xs ) ) +0 );
    val[4] = '\0';
    write ( of,val,4 );
    write ( of,"\003\000\006SpawnY",9 );
    val[0] = * ( ( ( char* ) ( &zs ) ) +3 );
    val[1] = * ( ( ( char* ) ( &zs ) ) +2 );
    val[2] = * ( ( ( char* ) ( &zs ) ) +1 );
    val[3] = * ( ( ( char* ) ( &zs ) ) +0 );
    val[4] = '\0';
    write ( of,val,4 );
    //int negx = -xs;
    write ( of,"\003\000\006SpawnZ",9 );
    val[0] = * ( ( ( char* ) ( &ys ) ) +3 );
    val[1] = * ( ( ( char* ) ( &ys ) ) +2 );
    val[2] = * ( ( ( char* ) ( &ys ) ) +1 );
    val[3] = * ( ( ( char* ) ( &ys ) ) +0 );
    val[4] = '\0';
    write ( of,val,4 );

    write ( of,"\004\000\012SizeOnDisk",13 );
    val[0] = * ( ( ( char* ) ( &totalsize ) ) +7 );//this is the total size of the chunk files, does not include this files size, nor disk space for directories
    val[1] = * ( ( ( char* ) ( &totalsize ) ) +6 );
    val[2] = * ( ( ( char* ) ( &totalsize ) ) +5 );
    val[3] = * ( ( ( char* ) ( &totalsize ) ) +4 );
    val[4] = * ( ( ( char* ) ( &totalsize ) ) +3 );
    val[5] = * ( ( ( char* ) ( &totalsize ) ) +2 );
    val[6] = * ( ( ( char* ) ( &totalsize ) ) +1 );
    val[7] = * ( ( ( char* ) ( &totalsize ) ) +0 );
    val[8] = '\0';
    write ( of,val,8 );

    write ( of,"\000",1 );//end of data coumpound

    write ( of,"\010\000\013GeneratedBy",14 );
    char str[256];
    strcpy ( str,"Dwarf Fortress To Minecraft by TroZ" );
    int len = strlen ( str );
    write ( of, ( ( char* ) & ( len ) ) +1,1 );
    write ( of,& ( len ),1 );
    write ( of,str,len );

    write ( of,"\000",1 );// end of unnamed compound


    close ( of );

    char filename[512];
    snprintf ( filename,511,"%s/%s",dirname,"level.dat" );
    filename[511]='\0';
    int res = compressFile ( out, "out.mcraw",filename );
    if ( res != Z_OK )
    {
        out.printerr ( "\nError compressing file (%d)\n",res );
    }
    else
    {
        out.print ( "Done!\nDirectory \'%s\'\n",dirname );
    }


    return res;
}

int getLight ( uint8_t *light,int xmax, int ymax, int zmax, int x, int y , int z, bool isSky )
{

    //is it out of bounds?
    if ( x<0 || y<0 || z<0 || x>=xmax || y>=ymax )
    {
        return 0;
    }
    if ( z>=zmax )
    {
        if ( isSky )
        {
            return 15;
        }
        else
        {
            return 0;
        }
    }

    int index = x + ( z * ymax +y ) * xmax;
    return light[index];
}

inline void lightCubeSky ( uint8_t *mcskylight, int xmax, int ymax, int zmax, int x, int y, int z, int opacity, int index )
{
    int skylightvert=max ( getLight ( mcskylight,xmax, ymax, zmax, x, y , z+1, true ),getLight ( mcskylight,xmax, ymax, zmax, x, y , z-1, true ) );
    int skylighthoriz = max (
                            max ( getLight ( mcskylight,xmax, ymax, zmax, x+1, y , z, true ),getLight ( mcskylight,xmax, ymax, zmax, x-1, y , z, true ) ),
                            max ( getLight ( mcskylight,xmax, ymax, zmax, x, y+1 , z, true ),getLight ( mcskylight,xmax, ymax, zmax, x, y-1 , z, true ) ) );
    skylighthoriz = ( skylighthoriz>0 ) ?skylighthoriz-1:0;
    int skylight = max ( skylightvert, skylighthoriz );
    if ( opacity>0 )
    {
        skylight=max ( skylight-opacity,0 );
    }
    mcskylight[index] = skylight;
}

inline void lightCubeBlock ( uint8_t *mcblocklight, int xmax, int ymax, int zmax, int x, int y, int z, int opacity, int index )
{
    int blocklightvert=max ( getLight ( mcblocklight,xmax, ymax, zmax, x, y , z+1, false ),getLight ( mcblocklight,xmax, ymax, zmax, x, y , z-1, false ) );
    int blocklighthoriz = max (
                              max ( getLight ( mcblocklight,xmax, ymax, zmax, x+1, y , z, false ),getLight ( mcblocklight,xmax, ymax, zmax, x-1, y , z, false ) ),
                              max ( getLight ( mcblocklight,xmax, ymax, zmax, x, y+1 , z, false ),getLight ( mcblocklight,xmax, ymax, zmax, x, y-1 , z, false ) ) );

    int blocklight = max ( max ( blocklightvert, blocklighthoriz ),getLight ( mcblocklight,xmax, ymax, zmax, x, y , z, false ) );
    if ( opacity>=0 )
    {
        blocklight=max ( blocklight-opacity-1,0 );
    }
    mcblocklight[index] = blocklight;
}

void calcLighting ( DFHack::color_ostream & out, uint8_t *mclayers,uint8_t *mcskylight,uint8_t *mcblocklight, int xmax, int ymax, int zmax )
{

    out.print ( "\nCalculating lighting...\n" );

    for ( int pass=15;pass>0;pass-- )
    {
        out.print ( "." );
        for ( int z=zmax-1;z>-1;z-- )
        {
            for ( int y=0;y<ymax;y++ )
            {
                for ( int x=0;x<xmax;x++ )
                {

                    int index = x + ( z * ymax +y ) * xmax;
                    int blocktype = mclayers[index];

                    //calc skylight
                    int opacity = cubeSkyOpacity[blocktype];
                    if ( opacity<15 )
                    {
                        lightCubeSky ( mcskylight, xmax, ymax, zmax, x, y, z,opacity, index );
                    }

                    //calc blocklight
                    opacity = cubeBlockOpacity[blocktype];
                    if ( opacity<0 )
                    {
                        mcblocklight[index] = -opacity;
                        lightCubeBlock ( mcblocklight, xmax, ymax, zmax, x, y, z, opacity, index );
                    }
                    else if ( opacity<15 )
                    {
                        lightCubeBlock ( mcblocklight, xmax, ymax, zmax, x, y, z, opacity, index );
                    }
                }
            }
        }
    }

    //final pass - fill only the partially lit objects
    out.print ( " ." );
    for ( int z=zmax-1;z>-1;z-- )
    {
        for ( int y=0;y<ymax;y++ )
        {
            for ( int x=0;x<zmax;x++ )
            {

                int index = x + ( z * ymax +y ) * xmax;
                int blocktype = mclayers[index];

                if ( cubePartialLit[blocktype] )
                {
                    //calc skylight
                    lightCubeSky ( mcskylight, xmax, ymax, zmax, x, y, z, index,0 );

                    //calc blocklight
                    lightCubeBlock ( mcblocklight, xmax, ymax, zmax, x, y, z, index,0 );
                }
            }
        }
    }

}

void calcLightingIndev ( DFHack::color_ostream & out, uint8_t *mcdata,uint8_t *mcskylight,uint8_t *mcblocklight, int mcxsquares, int mcysquares, int mczsquares )
{
    out.print ( "  ." );
    int total = mcxsquares * mcysquares * mczsquares;
    for ( int i = 0; i<total; i++ )
    {
        mcdata[i] = 0x0f & ( max ( mcskylight[i],mcblocklight[i] ) );
    }
}



void replacespaces ( char* str )
{
    while ( *str!='\0' )
    {
        if ( *str==' ' || ( !isalnum ( *str ) && *str!='_' && *str!='.' && *str!='-' ) )
            *str='_';
        *str = tolower ( *str );
        str++;
    }
}

uint32_t getMapIndex ( uint32_t x,uint32_t y,uint32_t z )
{
    return ( ( z&0x1ff ) <<22 ) | ( ( x&0x7ff ) <<11 ) | ( y&0x7ff );
}


void addUnknown ( TiXmlElement *uio, TiXmlElement *section,const char* name,const char *data, int stattype )
{

    if ( uio!=NULL )
    {
        TiXmlElement *sect = uio->FirstChildElement ( section->Value() );
        if ( sect==NULL )
        {
            sect = new TiXmlElement ( section->Value() );
            uio->LinkEndChild ( sect );
        }

        TiXmlElement *elm = sect->FirstChildElement ( name );
        if ( elm==NULL )  //add perfect match to unimplemented objects
        {
            elm = new TiXmlElement ( name );
            elm->SetAttribute ( "mat",makeAirArray() );
            if ( data!=NULL )
            {
                elm->SetAttribute ( "data",data );
            }
            sect->LinkEndChild ( elm );
            if ( stattype>-1 )
                stats[stattype][UNSEEN]++;
        }
    }

}

uint8_t* getMaterial ( DFHack::color_ostream & out, TiXmlElement *uio,int x, int y, int z,const  char* basicmaterial, int variant=0,
                       const char* fullname = NULL, const char* specificmaterial = NULL, const char* constmat = NULL, bool addstats = true )
{

    //check order
    //basic material, varient, description, specific material, construction material
    //basic material, varient, description, specific material
    //basic material, varient, description
    //basic material, description, specific material, construction material
    //basic material, description, specific material
    //basic material, description
    //basic material, varient, specific material, construction material
    //basic material, varient, specific material
    //basic material, varient
    //basic material, specific material, construction material
    //basic material, specific material
    //basic material
#define NUM_OBJECT_CHECKS 12

    char loc[NUM_OBJECT_CHECKS][256]; //description of current material for lookup in materials
    const char* smat = NULL;
    if ( NULL!=specificmaterial && specificmaterial[0]!='\0' )
    {
        smat = specificmaterial;
    }

    if ( smat!=NULL )
    {
        if ( constmat!=NULL )
        {
            if ( fullname!=NULL )
            {
                snprintf ( loc[11],255,"%s.%d.%s.%s-%s",   basicmaterial,  variant,    fullname,   smat,   constmat );
                snprintf ( loc[8],255,"%s.%s.%s-%s",       basicmaterial,  fullname,   smat,   constmat );
            }
            else
            {
                loc[11][0]='\0';
                loc[8][0]='\0';
            }
            snprintf ( loc[5],255,"%s.%d.%s-%s",   basicmaterial,  variant,    smat,   constmat );
            snprintf ( loc[2],255,"%s.%s-%s",  basicmaterial,  smat,   constmat );
        }
        else
        {
            loc[11][0]='\0';
            loc[8][0]='\0';
            loc[5][0]='\0';
            loc[2][0]='\0';
        }
        if ( fullname!=NULL )
        {
            snprintf ( loc[10],255,"%s.%d.%s.%s",  basicmaterial,  variant,    fullname,   smat );
            snprintf ( loc[7],255,"%s.%s.%s",      basicmaterial,  fullname,   smat );
        }
        else
        {
            loc[10][0]='\0';
            loc[7][0]='\0';
        }
        snprintf ( loc[4],255,"%s.%d.%s",  basicmaterial,  variant,    smat );
        snprintf ( loc[1],255,"%s.%s",     basicmaterial,  smat );
    }
    else
    {
        loc[11][0]='\0';
        loc[10][0]='\0';
        loc[8][0]='\0';
        loc[7][0]='\0';
        loc[5][0]='\0';
        loc[4][0]='\0';
        loc[2][0]='\0';
        loc[1][0]='\0';
    }
    if ( fullname!=NULL )
    {
        snprintf ( loc[9],255,"%s.%d.%s",  basicmaterial,  variant,    fullname );
        snprintf ( loc[6],255,"%s.%s", basicmaterial,  fullname );
    }
    else
    {
        loc[9][0]='\0';
        loc[6][0]='\0';
    }
    snprintf ( loc[3],255,"%s.%d", basicmaterial,  variant );
    snprintf ( loc[0],255,"%s",        basicmaterial );


    for ( int r=0;r<NUM_OBJECT_CHECKS;r++ )
        replacespaces ( loc[r] );

    uint8_t *material = NULL;

    //find the best description we have for the location
    int bpos=NUM_OBJECT_CHECKS-1;
    char *best = loc[bpos];
    while ( best[0]=='\0' && bpos>-1 )
    {
        bpos--;
        best = loc[bpos];
    }

    //find the most descriptive object that matches the current location
    if ( dfMats.find ( best ) ==dfMats.end() )
    {
        //no perfect match - find a good match and add perfect to list of unimplemented objects
        char* use=NULL;
        for ( int l= ( NUM_OBJECT_CHECKS-1 );l>-1&&use==NULL;l-- )
        {
            if ( loc[l][0]!='\0' && dfMats.find ( loc[l] ) !=dfMats.end() )
            {
                use=loc[l];
                material=dfMats[use];
            }
        }
        if ( use!=NULL )
        {
            if ( addstats )
            {
                if ( strstr ( use,"." ) ==NULL && strstr ( use,"air" ) !=use && strstr ( use,"logs" ) !=use && strstr ( use,"grass" ) !=use && strstr ( use,"unknown" ) !=use )
                {
                    //DFConsole->print("location %d,%d,%d is %s\tusing %s\n",x,y,z,best,use);
                }
                stats[MATERIALS][IMPERFECT]++;
                addUnknown ( uio, xmlmaterials, best, NULL, MATERIALS );
            }
            else
            {
                addUnknown ( uio, xmlmaterials, best, NULL, -1 );
            }
        }
        else
        {
            //not even a basic object found - create a basic object for hack/df2mc.xml
            if ( addstats )
            {
                out.print ( "location %d,%d,%d is %s\tNOT FOUND!\tcreating %s as air\n",x,y,z,best,loc[0] );
                stats[MATERIALS][UNKNOWN]++;

                TiXmlElement * ss = new TiXmlElement ( loc[0] );
                ss->SetAttribute ( "mat",makeAirArray() );
                ss->SetAttribute ( "data","" );
                xmlmaterials->LinkEndChild ( ss );

                material = makeAirArrayInt();
                dfMats[loc[0]] = material;
            }

            addUnknown ( uio, xmlmaterials, best, NULL, addstats?MATERIALS:-1 );
        }

    }
    else
    {
        // perfect match found (this is probably rare) use this object
        //DFConsole->print("location %d,%d,%d is %s\t\n",x,y,z,best);
        material=dfMats[best];
        if ( addstats )
            stats[MATERIALS][PERFECT]++;
    }

    return material;
}

uint8_t* getTerrain ( DFHack::color_ostream & out, TiXmlElement *uio,int x, int y, int z,const char* classname,const  char* basicmaterial, int variant=0,
                      const char* fullname = NULL,const char* specificmaterial = NULL, const char* constmat = NULL,bool addstats=NULL )  //todo, change addstats to an int and see what breaks - missing constmat
{

    //first get the material
    uint8_t* mat = getMaterial ( out, uio,x,y,z,basicmaterial, variant,fullname,specificmaterial, constmat, addstats );

    //now get the terrian
    char name[256];
    strncpy ( name,classname,255 );
    name[255]='\0';
    replacespaces ( name );
    std::map<std::string,uint8_t*>::iterator it = terrain.find ( name );
    int size = squaresize*squaresize*squaresize;
    if ( it==terrain.end() )
    {
        if ( addstats )
        {
            addUnknown ( uio, xmlterrain, classname, NULL, TERRAIN );
            return makeAirArrayInt();
        }
        else
        {
            return NULL;
        }
    }
    else
    {
        stats[TERRAIN][PERFECT]++;
    }

    //now make the material in the shape of the terain
    uint8_t *terrain = new uint8_t[size*2];
    for ( int i=0;i<size;i++ )
    {
        if ( it->second[i]==255 )
        {
            terrain[i]=mat[i];
        }
        else
        {
            terrain[i]=it->second[i];
        }
        terrain[size+i]=it->second[size+i];
    }

    return terrain;
}


uint8_t* getFlow ( TiXmlElement *uio,int x, int y, int z,const char* classname,const  char* basicmaterial, int variant=0,
                   const char* fullname = NULL,const char* specificmaterial = NULL,bool addstats=true )
{

    //first get the material
    //uint8_t* mat = getMaterial(uio,x,y,z,basicmaterial, variant,fullname,specificmaterial);

    //now get the terrian
    char name[256];
    strncpy ( name,classname,255 );
    name[255]='\0';
    replacespaces ( name );
    std::map<std::string,uint8_t*>::iterator it = flows.find ( name );
    int size = squaresize*squaresize*squaresize;
    if ( it==flows.end() )
    {
        addUnknown ( uio, xmlflows, classname, NULL, FLOWS );
        return makeAirArrayInt();
    }
    else
    {
        stats[FLOWS][PERFECT]++;
    }

    //return terrain;
    return it->second;
}

uint8_t* getPlant ( DFHack::color_ostream & out, TiXmlElement *uio,int x, int y, int z,const char* classname,const  char* basicmaterial, int variant=0,
                    const char* fullname = NULL,const char* specificmaterial = NULL,bool addstats=true )
{

    //first get the material
    uint8_t* mat = getMaterial ( out, uio,x,y,z,basicmaterial, variant,fullname,specificmaterial );

    //now get the terrian
    char name[256];
    strncpy ( name,classname,255 );
    name[255]='\0';
    replacespaces ( name );
    std::map<std::string,uint8_t*>::iterator it = plants.find ( name );
    int size = squaresize*squaresize*squaresize;
    if ( it==plants.end() )
    {
        //didn't find specific plant type, try generic
        char *pos = strchr ( name,'.' );
        if ( pos!=NULL )
        {
            *pos='\0';
            it = plants.find ( name );
        }
        if ( it==plants.end() )
        {
            addUnknown ( uio, xmlplants, classname, NULL, PLANTS );
            return makeAirArrayInt();
        }
        else
        {
            stats[PLANTS][IMPERFECT]++;
        }
    }
    else
    {
        stats[PLANTS][PERFECT]++;
    }

    //now make the material in the shape of the terain
    uint8_t *plant = new uint8_t[size*2];
    for ( int i=0;i<size;i++ )
    {
        if ( it->second[i]==255 )
        {
            plant[i]=mat[i];
        }
        else
        {
            plant[i]=it->second[i];
        }
        plant[size+i]=it->second[size+i];
    }

    return plant;
}

uint8_t* getBuilding ( DFHack::color_ostream & out, TiXmlElement *uio,int x, int y, int z,const char* classname, int direction, const  char* basicmaterial,
                       const char* fullname = NULL,const char* specificmaterial = NULL,bool addstats=true )
{

    //first get the material
    uint8_t* mat = getMaterial ( out, uio,x,y,z,basicmaterial, 0,fullname,specificmaterial,NULL,addstats );

    //now get the building
    char name[256];
    snprintf ( name,255,"%s.%d",classname,direction );
    name[255]='\0';
    replacespaces ( name );
    int size = squaresize*squaresize*squaresize;
    std::map<std::string,uint8_t*>::iterator it = buildings.find ( name );
    if ( it==buildings.end() )
    {

        char* pos = strchr ( name,'.' );//check again without building position
        if ( pos!=NULL )
        {
            char name2[256];
            *pos='\0';
            snprintf ( name2,255,"%s.%d",name,direction );
            name2[255]='\0';
            it = buildings.find ( name2 );

            if ( addstats && it==buildings.end() )
            {
                it = buildings.find ( classname ); //now it position but not direction (if not looking if the direction exists)

                if ( it==buildings.end() )   //finally looks for just the raw classname withouth position or direction
                {
                    it = buildings.find ( name );
                }
            }
        }

        if ( it==buildings.end() )
        {
            if ( addstats )
            {
                addUnknown ( uio, xmlbuildings, classname, NULL, BUILDINGS );
                return makeAirArrayInt();
            }
            else
            {
                return NULL;
            }
        }
        else
        {
            stats[BUILDINGS][IMPERFECT]++;
        }
    }
    else if ( addstats )
    {
        stats[BUILDINGS][PERFECT]++;
    }

    //now make the material in the shape of the terain
    uint8_t *building = new uint8_t[size*2];
    for ( int i=0;i<size;i++ )
    {
        if ( it->second[i]==255 )
        {
            building[i]=mat[i];
        }
        else
        {
            building[i]=it->second[i];
        }
        building[size+i]=it->second[size+i];
    }

    return building;
}


void addObject ( uint8_t* mclayers, uint8_t* mcdata, uint8_t *object, int dfx, int dfy, int dfz, int xoffset, int yoffset, int zoffset, int mcxsquares, int mcysquares,bool overwrite=false )
{

    //now copy object in to mclayer array

    int mcx = dfx * squaresize - ( xoffset*SQUARESPERBLOCK*squaresize );
    int mcy = dfy * squaresize - ( yoffset*SQUARESPERBLOCK*squaresize );
    //int mcz = dfz * squaresize - (zoffset*squaresize);
    int mcz = ( zoffset*squaresize ) + 1;
    int pos = 0;
    int pos2 = squaresize*squaresize*squaresize;

    for ( int oz=0;oz<squaresize;oz++ )
    {
        for ( int oy=0;oy<squaresize;oy++ )
        {
            for ( int ox=0;ox<squaresize;ox++ )
            {

                //Index = x + (y * Depth + z) * Width //where y is up down
                //int x = (mcx+ox);
                //int y = (mcy+oy);
                //int z = (mcz+2-oz);
                //also MC's X and Z seem rotated to the assumed DF X and Y - correcting so that sun in MC rises in DF East
                int x = ( mcy+oy );
                int y = ( mcxsquares-1 )- ( mcx+ox );
                int z = ( mcz+2-oz );
                int idx = x + ( z * mcysquares +y ) * mcxsquares;
                if ( overwrite || mclayers[idx]==0 )
                {
                    if ( safesand && sandhash.find ( object[pos] ) !=sandhash.end() )
                    {
                        hash_set<int>::iterator it = supporthash.find ( mclayers[x + ( ( z-1 ) * mcysquares +y ) * mcxsquares] );
                        if ( it != supporthash.end() )
                        {
                            //this is sand (or other gravity obaying block) on the bottom level with air (or other nonsupport block) below it and safe sand is on, replace the sand
                            mclayers[idx]=safesand;
                            mcdata[idx]=object[pos2];
                        }
                        else
                        {
                            mclayers[idx]=object[pos];
                            mcdata[idx]=object[pos2];
                        }
                    }
                    else
                    {
                        mclayers[idx]=object[pos];
                        mcdata[idx]=object[pos2];
                    }
                }
                pos++;
                pos2++;
            }
        }
    }
}


void getObjDir ( DFHack::color_ostream & out, DFHack::mapblock40d *Bl,TiXmlElement *uio,char *dir,int x,int y,int z,int bx, int by,const char* classname,
                 const char* mat, int varient,const char* full,const char* specmat,const char* consmat,const bool building = false )
{
    //this will find and place in the string dir the letters of the high side of a ramp

    DFHack::mapblock40d *Block;
    DFHack::mapblock40d *BlockLast = Bl;
    DFHack::mapblock40d B;
    //TileClass tc;
    df::tiletype_shape ts;

    int blockx,blocky,offsetx,offsety;
    int lastblockx = bx;
    int lastblocky = by;

    // Ramps are named with numbers to indicate which side have 'walls' - the high sides
    // other sides are assumed to be low, but may also have ramps (does this need to be handled?)
    // assuming ramp is at position 5 (which will never be shown as high) walls are numbered like this:
    // 1 2 3
    // 4 5 6
    // 7 8 9
    //
    // numbers will be listed in number order, a wall to the south, southwest and west is 478 not 874 or other possibilities
    //
    // we first try to find a defination of a ramp using all directions (numbers)
    // if we don't find a match, we then then limit to directly north, south , east, and west (2,4,6,8)
    // so if a wall to the south, southwest and west is not found as 478, we will check if 48 exists and if not, use no numbers

    //ok first we will try to find an exact match for the ramp
    uint8_t* obj = NULL;
    int step = 1;
    int start = 1;
    //this DO tries all numbers first, and only the even ones in the second pass
    do
    {
        char *pos=dir;

        for ( int i=start;i<10;i+=step )
        {

            if ( i==5 ) continue;//don't check self

            int ox= ( ( i-1 ) %3 )-1;
            int oy= ( ( i-1 ) /3 )-1;


            blockx  =   ( x+ox ) / SQUARESPERBLOCK;
            offsetx =   ( x+ox ) % SQUARESPERBLOCK;
            blocky  =   ( y+oy ) / SQUARESPERBLOCK;
            offsety =   ( y+oy ) % SQUARESPERBLOCK;
            //see if we already have that block (we should most of the time)
            if ( blockx == lastblockx && blocky == lastblocky )
            {
                Block=BlockLast;
            }
            else
            {
                if ( Maps::getBlock ( blockx,blocky,z ) )
                {
                    Maps::ReadBlock40d ( blockx,blocky,z, &B );
                    Block=&B;
                    lastblockx = blockx;
                    lastblocky = blocky;
                    BlockLast = Block;
                }
                else
                {
                    Block=NULL;
                }
            }
            //check location for wall or fortification
            if ( Block!=NULL )
            {
                ts = tileShape(Block->tiletypes[offsetx][offsety]);
                if ( ts == tiletype_shape::WALL || ts == tiletype_shape::FORTIFICATION )
                {
                    *pos= ( '0'+i );
                    pos++;
                }
            }

        }

        *pos='\0';
        //try to find a match
        char test[128];

        if ( !building )
        {
            snprintf ( test,127,"%s%s",classname,dir );
            test[127]='\0';
            obj = getTerrain ( out, uio,x,y,z,test,mat,varient,full,specmat,NULL,false );
        }
        else
        {
            int idir = atoi ( dir );
            obj = getBuilding ( out, uio,x,y,z,classname,-idir,mat,full,specmat,false );
        }

        //setup of next DO loop, if needed
        step++;
        start++;
    }
    while ( obj==NULL && start < 3 );

    if ( obj==NULL )   //no match found - use 'undirected' ramp
    {
        dir[0]='\0';
    }

}

int getBuildingDir ( DFHack::color_ostream & out, map<uint32_t,myBuilding> Buildings,DFHack::mapblock40d *Bl,TiXmlElement *uio,int x,int y,int z,int bx, int by,
                     const char* thisBuilding,const char*mat, const char* full,const char* specmat,const char* buildingToFace )
{

    //similar routine to above (getRampDir), but no need to look in different map blocks
    uint8_t* obj = NULL;
    int step = 1;
    int start = 1;
    if ( strcmp ( thisBuilding,"chair" ) ==0 )
    {
        start = 1;
    }
    char dir[16];
    //this DO tries all numbers first, and only the even ones in the second pass
    if ( buildingToFace != NULL && buildingToFace[0]!='\0' )
    {
        do
        {
            char *pos = dir;

            for ( int i=start;i<10;i+=step )
            {

                if ( i==5 ) continue;//don't check self

                int ox= ( ( i-1 ) %3 )-1;
                int oy= ( ( i-1 ) /3 )-1;

                uint32_t index = getMapIndex ( x+ox,y+oy,z );
                map<uint32_t,myBuilding>::iterator it;
                it = Buildings.find ( index );
                if ( it!=Buildings.end() )
                {
                    myBuilding mb = it->second;

                    if ( stricmp ( mb.type,buildingToFace ) ==0 )
                    {
                        *pos= ( '0'+i );
                        pos++;
                    }
                }
            }
            *pos='\0';

            if ( dir[0]!='\0' )
            {
                int idir;
                idir = atoi ( dir );
                obj = getBuilding ( out, uio,x,y,z,thisBuilding,idir,mat,full,specmat,false );
            }

            //setup of next DO loop, if needed
            step++;
            start++;
        }
        while ( obj==NULL && start < 3 );
    }

    if ( obj==NULL )   //no match found - now check for walls
    {
        dir[0]='\0';
        getObjDir ( out, Bl,uio,dir,x,y,z,bx,by,thisBuilding,mat,0,full,specmat,NULL,true );

        return - ( atoi ( dir ) );
    }
    else
    {
        return atoi ( dir );
    }
}

int findLevels ( int xmin,int xmax,int ymin,int ymax,int zmax,uint32_t typeToFind )
{

    DFHack::mapblock40d Block;
    int count = 0;

    for ( int z=0;z<zmax;z++ )
    {

        bool found = false;

        for ( int x=xmin; ( x<xmax ) && ( !found );x++ )
        {
            for ( int y=ymin; ( y<ymax ) && ( !found );y++ )
            {

                if ( !Maps::getBlock ( x,y,z ) )
                    continue;
                Maps::ReadBlock40d ( x,y,z, &Block );

                for ( uint32_t xx=0;xx<SQUARESPERBLOCK&&!found;xx++ )
                {
                    for ( uint32_t yy=0;yy<SQUARESPERBLOCK&&!found;yy++ )
                    {
                        int type = tileShape(Block.tiletypes[xx][yy]);
                        int val = ( 1<<type ) &typeToFind;
                        if ( val )
                        {
                            found=true;
                        }
                    }
                }
            }
        }

        if ( found )
        {
            limitz[z]=255;
            count++;
        }
    }

    return count;
}

// FIXME: den of evil. nuke. nuke. nuke. nuke. nuke. BZZT@!
void getConsMats ( DFHack::color_ostream & out, std::string & mat, std::string & consmat, int type, int idx, int form, char* tempstr, int x, int y, int z )
{

    consmat="unknown";
    if ( type == 0 )
    {
        if ( idx != -1 && idx< world->raws.inorganics.size() )
            consmat = world->raws.inorganics[idx]->id;
        else consmat = "inorganic";
    }
    // FIXME: this is WRONG!
    else if ( type == 420 || type == 421 || type == 422)
    {
        if ( idx != -1 && idx< world->raws.plants.all.size() )
            consmat = world->raws.plants.all[idx]->id;
        else consmat = "organic";
    }
    /*
    else if ( type == 421 )   // is 421 plant material? (rope reed fiber rope seemed to be 421 14)
    {
        if ( idx != 0xffffffff && idx<Mats->df_plants->size() )
            consmat = Mats->df_plants->at(idx)->ID;
        else consmat = "plant";
        DFConsole->print ( "Semi-known Construction Material at %d, %d, %d: %s  -form:%d, type:%d\n",x,y,z,consmat.c_str(),form,type );
    }
    else if ( type == 422 )   // is 422 also plant material? (Pigtail fiber bags seemed to be 422 1)
    {
        if ( idx != 0xffffffff && idx<Mats->df_plants->size() )
            consmat = Mats->df_plants->at(idx)->ID;
        else consmat = "plant";
        DFConsole->print ( "Semi-known Construction Material at %d, %d, %d: %s  -form:%d, type:%d\n",x,y,z,consmat.c_str(),form,type );
    }
    */
    else if ( type == 7 )
    {
        consmat = "coal";
    }
    else if ( type == 9 )
    {
        consmat = "ashes";
    }
    else if ( type == 3 || type ==  13 )    //3 is .31.16  - 13 is .28.40d
    {
        consmat = "greenglass";
    }
    else if ( type == 4 || type ==  14 )   //4 is .31.16  - 14 is .28.40d
    {
        consmat = "clearglass";
    }
    else if ( type == 5 ||type ==  15 )   //guessing at the 5, can't get rock_crystal to make crystal glass
    {
        consmat = "crystalglass";
    }
    else if ( type ==  18 )
    {
        consmat = "charcoal";
    }
    else if ( type == 8 || type ==  19 )   //8 is .31.16  - 19 is .28.40d
    {
        consmat = "potash";
    }
    else if ( type ==  20 )
    {
        consmat = "ash";
    }
    else if ( type == 10 || type ==  21 )   //10 is .31.16  - 21 is .28.40d
    {
        consmat = "perlash";
    }
    else if ( type ==  39 )
    {
        if ( idx != -1 && idx<world->raws.creatures.all.size() )
            consmat = world->raws.creatures.all[idx]->creature_id;
        else consmat = "animal";
        out.print ( "Semi-known Construction Material at %d, %d, %d: %s  -form:%d, type:%d\n",x,y,z,consmat.c_str(),form,type );

        consmat = "soap";
        //idx I believe is the creature type (I had 103 for One-humped Camel Soap), but nore sure where list of creatures is, and at this point, I'm not making soaps look different
    }
    else
    {
        snprintf ( tempstr,255,"unknown_%d_%d",type,idx );
        tempstr[255]='\0';
        consmat = tempstr;
        if ( type>0 )
            out.print ( "Unknown Construction Material at %d, %d, %d: %s  -form:%d\n",x,y,z,tempstr,form );
    }
    switch ( form )
    {
    case item_type::BAR:
        mat="bars";
        break;
    case item_type::BLOCKS:
        mat="blocks";
        break;
    case item_type::BOULDER:
        mat="stone";
        break;
    case item_type::WOOD:
        mat="logs";
        break;
    default:
        mat="unknown";
    }
}


void convertDFBlock ( color_ostream & out, vector< vector <uint16_t> > layerassign,
                      map<uint32_t,myConstruction> & Constructions, map<uint32_t,myBuilding> & Buildings, map<uint32_t,std::string> & vegs,
                      TiXmlElement *uio, uint8_t* mclayers, uint8_t* mcdata,
                      uint32_t dfblockx, uint32_t dfblocky, uint32_t zzz, uint32_t zcount,
                      uint32_t xoffset, uint32_t yoffset, int mcxsquares, int mcysquares )
{

    DFHack::mapblock40d Block;

    // read data
    Maps::ReadBlock40d ( dfblockx,dfblocky,zzz, &Block );
    //Maps->ReadTileTypes(x,y,z, &tiletypes);
    //Maps->ReadDesignations(x,y,z, &designations);

    vector<df::block_square_event_mineralst *> veins;
    vector<df::block_square_event_material_spatterst *> splatter;
    Maps::SortBlockEvents ( dfblockx,dfblocky,zzz,&veins,NULL,&splatter );
    char tempstr[256];
    t_feature local, global;

    Maps::ReadFeatures(dfblockx,dfblocky,zzz,&local, &global);
    for ( uint32_t dfoffsetx=0;dfoffsetx<SQUARESPERBLOCK;dfoffsetx++ )
    {
        for ( uint32_t dfoffsety=0;dfoffsety<SQUARESPERBLOCK;dfoffsety++ )
        {

            uint32_t dfx = dfblockx*SQUARESPERBLOCK + dfoffsetx;
            uint32_t dfy = dfblocky*SQUARESPERBLOCK + dfoffsety;
            df::tiletype tiletype = Block.tiletypes[dfoffsetx][dfoffsety]; //this is the type of terrian at the location (or at least the tiletype.c(lass) is)

            df::tile_designation &des = Block.designation[dfoffsetx][dfoffsety]; //designations at this location

            std::string mat; //item material
            std::string consmat;//material of construction

            int16_t temp_inorganic = -1;
            df::tiletype_material tilemat = tileMaterial(tiletype);
            if ( tilemat == tiletype_material::FEATURE )
            {
                // global feature overrides
                int16_t idx = Block.global_feature;
                if ( idx != -1 && Block.designation[dfoffsetx][dfoffsety].bits.feature_global)
                {
                    if(global.type != -1 && global.main_material == 0)
                    {
                        temp_inorganic = global.sub_material;
                    }
                    else
                    {
                        temp_inorganic = -1;
                    }
                }
                // local feature overrides
                idx = Block.local_feature;
                if ( idx != -1 && Block.designation[dfoffsetx][dfoffsety].bits.feature_local)
                {
                    if(local.type != -1 && local.main_material == 0)
                    {
                        temp_inorganic = local.sub_material;
                    }
                    else
                    {
                        temp_inorganic = -1;
                    }
                }
                if ( temp_inorganic!=-1 )
                {
                    mat = world->raws.inorganics[temp_inorganic]->id;
                }
                else
                {
                    mat.clear();
                }
            }
            else if ( tilemat == tiletype_material::SOIL || tilemat == tiletype_material::STONE )
            {
                //try to find material in base layer type

                uint8_t test = Block.designation[dfoffsetx][dfoffsety].bits.biome;
                if ( test < sizeof ( Block.biome_indices ) )
                {
                    //otherwise memory error - not sure how to handle, but shouldn't happen
                    temp_inorganic = layerassign[Block.biome_indices[test]][Block.designation[dfoffsetx][dfoffsety].bits.geolayer_index];
                }

                //find material in veins
                //iterate through the bits
                for ( int v = 0; v < ( int ) veins.size();v++ )
                {
                    // and the bit array with a one-bit mask, check if the bit is set
                    bool set = !! ( ( ( 1 << dfoffsetx ) & veins[v]->tile_bitmask[dfoffsety] ) >> dfoffsetx );
                    if ( set )
                    {
                        // store matgloss
                        temp_inorganic = veins[v]->inorganic_mat;
                    }
                }
                if ( temp_inorganic!=-1 )
                {
                    mat = world->raws.inorganics[temp_inorganic]->id;
                }
                else
                {
                    mat.clear();
                }
            }
            else if (tilemat == tiletype_material::CONSTRUCTION)
            {
                //find construction and locate it's material
                mat.clear();
                consmat.clear();
                uint32_t index = getMapIndex ( dfx,dfy,zzz );
                map<uint32_t,myConstruction>::iterator it;
                it = Constructions.find ( index );
                if ( it!=Constructions.end() )
                {
                    getConsMats ( out, mat,  consmat, it->second.mat_type, it->second.mat_idx, it->second.form, tempstr,dfx,dfy,zzz );
                }
                else
                {
                    mat="unknown";
                    consmat="unknown";
                }
            }

            char classname[128];
            std::string plant;
            classname[0]='\0';
            int variant = tileVariant(tiletype);
            uint8_t* object = NULL;
            switch ( tileShape(tiletype) )
            {
            case tiletype_shape::TREE:
            case tiletype_shape::SAPLING:
            case tiletype_shape::SHRUB:
            {
                map<uint32_t,std::string>::iterator vit;
                vit = vegs.find ( getMapIndex ( dfx,dfy,zzz ) );
                if ( vit!=vegs.end() )
                {
                    plant = vit->second;
                    snprintf ( classname,127,"%s.%s",TileClassNames[tileShape(tiletype)],plant.c_str() );
                }
                else
                {
                    out.print ( "Cant find plant that should already be defined!\n" );
                }

                object = getPlant ( out, uio,dfx, dfy, zzz,classname, TileMaterialNames[tileMaterial(tiletype)], variant, tileName(tiletype), mat.c_str() );
            }
            break;
            case tiletype_shape::RAMP:
            {
                if ( classname[0]=='\0' )
                {
                    char dir[16];
                    getObjDir ( out, &Block,uio,dir,dfx,dfy,zzz,dfblockx,dfblocky,"ramp",TileMaterialNames[tileMaterial(tiletype)], variant, tileName(tiletype), mat.c_str(),consmat.c_str() );
                    snprintf ( classname,127,"%s%s",TileClassNames[tileShape(tiletype)],dir );
                }
            }
            break;
            case tiletype_shape::STAIR_UP:
            case tiletype_shape::STAIR_DOWN:
            case tiletype_shape::STAIR_UPDOWN:
            {
                if ( classname[0]=='\0' )
                {
                    snprintf ( classname,127,"%s%d",TileClassNames[tileShape(tiletype)],zcount%4 );//z$4 is the level height mod 4 so you can do spiral stairs or alterante sides if object defination of 0 = 2 and 1 = 3
                }
            }
            break;
            default:
                if ( classname[0]=='\0' )
                {
                    if ( directionalWalls )
                    {
                        char dir[16];
                        getObjDir ( out, &Block,uio,dir,dfx,dfy,zzz,dfblockx,dfblocky,TileClassNames[tileShape(tiletype)],TileMaterialNames[tileMaterial(tiletype)], variant, tileName(tiletype), mat.c_str(),consmat.c_str() );
                        snprintf ( classname,127,"%s%s",TileClassNames[tileShape(tiletype)],dir );
                    }
                    else
                    {
                        strncpy ( classname,TileClassNames[tileShape(tiletype)],127 );
                    }

                    //FIXME: smooth is a property of tile. smooth is also a designation meant to make dwarves avtualy do the smoothing.
                    // See tiletype_special::SMOOTH
                    if ( tileName(tiletype)!=NULL && strnicmp ( tileName(tiletype),"smooth",5 ) ==0 )  //doing this as des.smooth never seems to be set.
                    {
                        variant+=10;//and if I could figure out engraved it would be 20 over base variant
                    }
                }

            }

            if ( tileMaterial(tiletype) == tiletype_material::FROZEN_LIQUID )  //ice. or solidified magma, although the game doesn't really support that by default ... :)
            {
                biome = 1;
            }

            if ( tileName(tiletype) == NULL )
            {
                out.print ( "Unknown tile type at %d,%d layer %d - id is %d, DFHAck needs description\n",dfx,dfy,zzz,tiletype );
                stats[TERRAIN][UNKNOWN]++;
            }

            if ( object==NULL )
                object = getTerrain ( out, uio,dfx, dfy, zzz,classname, TileMaterialNames[tileMaterial(tiletype)], variant, tileName(tiletype), mat.c_str(), consmat.c_str(),true );

            //now copy object in to mclayer array
            addObject ( mclayers, mcdata, object,  dfx,  dfy, zzz, xoffset, yoffset, zcount, mcxsquares, mcysquares );


            //add tree top if tree
            if ( ( tileShape(tiletype) == tiletype_shape::TREE ) && ( ( zcount+1 ) < limitlevels ) )
            {
                snprintf ( classname,127,"%s.%s","treetop",plant.c_str() );
                object = getPlant ( out, uio,dfx, dfy, zzz,classname, "air", variant, tileName(tiletype), mat.c_str() );
                if ( object!=NULL )
                    addObject ( mclayers, mcdata, object,  dfx,  dfy, zzz+1, xoffset, yoffset, zcount+1, mcxsquares, mcysquares );
            }


            //Add building if any (furnaces/forge to furnace, others to workbench, make 'tables'/chests to block impassible squares
            uint32_t index = getMapIndex ( dfx,dfy,zzz );
            map<uint32_t,myBuilding>::iterator it;
            it = Buildings.find ( index );
            if ( it!=Buildings.end() )
            {
                myBuilding mb = it->second;
                mat="unknown";
                char *specmat = NULL;
                int form = item_type::BLOCKS;
                if ( mb.material.type == 0 )
                {
                    form = item_type::BOULDER;
                }
                else if ( mb.material.type == 420 )
                {
                    form = item_type::WOOD;
                }
                else
                {
                    //todo, how to tell if blocks of material as opposed to raw? or metal ?
                    //constr_bar
                }

                getConsMats ( out, mat, consmat, mb.material.type, mb.material.index, form, tempstr, dfx, dfy, zzz );

                char building[256];
                snprintf ( building,255,"%s.%s",mb.type,mb.desc );
                building[255]='\0';

                string toFace = "";
                map<string,string>::iterator it = buildingNeighbors.find ( mb.type );
                if ( it != buildingNeighbors.end() )
                {
                    toFace = it->second;
                }
                int dir = getBuildingDir ( out, Buildings, &Block, uio,dfx,dfy,zzz,dfblockx,dfblocky,building,mat.c_str(),"building",specmat,toFace.c_str() );

                snprintf ( building,255,"%s.%s",mb.type,mb.desc );
                building[255]='\0';

                object = getBuilding ( out, uio,dfx, dfy, zzz, building, dir, mat.c_str(), "building", specmat );
                if ( object!=NULL )
                {
                    addObject ( mclayers, mcdata, object,  dfx,  dfy, zzz, xoffset, yoffset, zcount, mcxsquares, mcysquares );
                }
                else
                {
                    out.printerr ( "Cant find building that should already be defined!\n" );
                }
            }

            //add object if any (mostly convert bins and barrels to chests)
            //TODO


            //add water/magma (liquid) if any
            if ( des.bits.flow_size>0 )
            {

                char *type = "magma";
                if ( des.bits.liquid_type == tile_liquid::Water )
                {
                    type = "water";
                }

                snprintf ( classname,127,"%s.%d",type,des.bits.flow_size );

                object = getFlow ( uio,dfx, dfy, zzz, classname, type ,des.bits.flow_size );
                if ( object!=NULL )
                {
                    addObject ( mclayers, mcdata, object,  dfx,  dfy, zzz, xoffset, yoffset, zcount, mcxsquares, mcysquares );
                }
            }


            //add torch if any 'dark' and is floor, and no mud (cave)
                        if ( tileShape(tiletype) == tiletype_shape::FLOOR && ( des.bits.light==0 || des.bits.outside==0 || des.bits.subterranean>0 ) )
            {
                //we will add torches to floors that aren't muddy as muddy floors are caves (or farms) usually, which we don't want lit,- we will claim the mud puts out the torch


                //check for mud
                bool mud = false;
                for ( int v = 0; v < ( int ) splatter.size() &&!mud;v++ )
                {
                    if ( splatter[v]->mat_type=0xC )  //magic number for mud - see cleanmap.cpp - turns out not really - this is any spatter, mud, blood, vomit, etc.
                    {
                        if ( splatter[v]->amount[dfoffsetx][dfoffsety]>0 )
                        {
                            mud=true; //yes, this tile is muddy
                        }
                    }
                }

                if ( !mud )
                {
                    //ok, good spot for torch - add if the random number is less than the chance depending on the 'dark' type, we don't want every floor to have a torch, just randomly placed
                    int percent = 0;
                    if ( des.bits.outside == 0 )
                        percent = max ( percent,torchPerInside );
                    if ( des.bits.light == 0 )
                        percent = max ( percent,torchPerDark );
                    if ( des.bits.subterranean > 0 ) //what are possible values
                        percent = max ( percent,torchPerSubter );

                    if ( ( rand() %100 ) <percent )
                    {
                        //ok, try to add a torch here

                        string toFace = "";
                        map<string,string>::iterator it = buildingNeighbors.find ( "torch" );
                        if ( it != buildingNeighbors.end() )
                        {
                            toFace = it->second;
                        }
                        int dir = getBuildingDir ( out, Buildings, &Block, uio,dfx,dfy,zzz,dfblockx,dfblocky,"torch","air",NULL,NULL,toFace.c_str() );

                        object = getBuilding ( out, uio,dfx, dfy, zzz, "torch", dir, "air" );
                        if ( object!=NULL )
                            addObject ( mclayers, mcdata, object,  dfx,  dfy, zzz, xoffset, yoffset, zcount, mcxsquares, mcysquares );

                    }
                }
            }
        }
    }
}

int convertMaps ( color_ostream & out )
{

    out.print ( "\nCalculating size limit...\n" );

    //setup

    uint32_t x_max,y_max,z_max;
    vector<DFHack::t_feature> global_features;
    std::map <DFHack::planecoord, std::vector<DFHack::t_feature *> > local_features;
    vector< vector <uint16_t> > layerassign;

    uint16_t cloudheight=0;

    //create xml file for a list of full unimplemented objects
    TiXmlElement *uio = NULL;
    if ( createUnknown )
    {
        TiXmlDocument *doc = new TiXmlDocument ( "hack/unimplementedobjects.xml" );
        bool loadOkay = doc->LoadFile();
        uio=doc->FirstChildElement ( "unimplemented_objects" );
        if ( uio==NULL )
        {
            uio = new TiXmlElement ( "unimplemented_objects" );
            doc->LinkEndChild ( uio );
            TiXmlComment *comment = new TiXmlComment ( "These elements are the most specific elements that make up the maps that you have converted\nIf something doesn't look right in the converted map, try to fix the object here,\nspecify how it should look in Minecraft, and copy that object into the hack/df2mc.xml file" );
            uio->LinkEndChild ( comment );
        }
    }

    // init the map
    if ( !Maps::IsValid() )
    {
        cerr << "Can't init map." << endl;
        return 103;
    }
    Maps::getSize ( x_max,y_max,z_max );

    out.print ( "DF Map size in \'blocks\' %dx%d with %d levels (a 3x3 block is one embark space)\n",x_max,y_max,z_max );
    out.print ( "DF Map size in squares %d, %d, %d\n",x_max*SQUARESPERBLOCK,y_max*SQUARESPERBLOCK,z_max );


    //level area limit
    x_max=min ( x_max,limitxmax );
    y_max=min ( y_max,limitymax );
    uint32_t xoffset = limitxmin;
    uint32_t yoffset = limitymin;
    //uint32_t zlevelcount = z_max;

    if ( xoffset>x_max || yoffset>y_max )
    {
        out.printerr ( "Invalid level size.\n" );
        return 21;
    }

    memset ( &limitz,0,1000 );


    //setup level area limit
    if ( limittype == LIMITTOP )
    {
        if ( limitairtokeep < 1 || limitairtokeep > ( z_max/2 ) )
        {
            //keep top limitlevels, disreguard air only levels
            for ( uint32_t zz=z_max-limitlevels;zz<z_max;zz++ )
            {
                limitz[zz]=255;
            }
            cloudheight=5;
        }
        else
        {
            //keep top limitlevels, but only airtokeep air levels
            //FIXME: this is now probably horribly broken because of that math magic.
            findLevels ( xoffset,x_max,yoffset,y_max,z_max, ( 1<<tiletype_shape::EMPTY ) ^0xffff );

            //ok levels that are not all air are now marked
            //we need to add 'airtokeep' levels to the top and then limit it to limitlevels
            uint32_t top = z_max;
            for ( uint32_t i=z_max;i>0;i-- )
            {
                if ( limitz[i]>0 )
                {
                    top=i;
                    break;
                }
            }
            top+=limitairtokeep;
            if ( top>z_max ) top = z_max;
            uint32_t bottom = top - limitlevels;
            //reset level marks
            for ( uint32_t i=0;i<z_max;i++ )
            {
                if ( i>top || i<=bottom )
                {
                    limitz[i]=0;
                }
                else
                {
                    limitz[i]=255;
                }
            }
            cloudheight=limitairtokeep/2;
        }
    }
    else if ( limittype == LIMITRANGE )
    {
        if ( limittoplevel<=0 )
        {
            limittoplevel = z_max;
        }
        for ( uint32_t i=limittoplevel;i> ( limittoplevel-limitlevels );i-- )
        {
            limitz[i]=255;
        }
        cloudheight=limitlevels;
    }
    else if ( limittype == LIMITSMART )
    {

        //keep 'interesting' levels and airtokeep air levels
        //FIXME: removed PILLAR and RIVER_BED here, because they aren't tile shapes anymore.
        findLevels ( xoffset,x_max,yoffset,y_max,z_max, ( 1<<tiletype_shape::FLOOR ) + ( 1<<tiletype_shape::FORTIFICATION ) + ( 1<<tiletype_shape::RAMP ) + (1<<tiletype_shape::RAMP_TOP ) );

        //ok, interesting levels are marked, mark the airtokeep levels above the top most interesting level
        uint32_t top = z_max;
        for ( uint32_t i=z_max;i>0;i-- )
        {
            if ( limitz[i]>0 )
            {
                top=i;
                break;
            }
        }
        top+=limitairtokeep;
        if ( top>z_max ) top = z_max;
        for ( uint32_t i=top;i>0;i-- )
        {
            if ( limitz[i]==0 )
            {
                limitz[i]=255;
            }
            else
            {
                break;
            }
        }
        //now count marked levels
        uint32_t bottom = 0;
        uint32_t count = 0;
        for ( uint32_t i=z_max;i>0;i-- )
        {
            if ( limitz[i]>0 )
            {
                bottom=i;
                count++;
            }
        }
        if ( count>limitlevels )
        {
            //too many levels, remove from bottom
            for ( uint32_t i=0;i<z_max&&count>=limitlevels;i++ )
            {
                if ( limitz[i]>0 )
                {
                    limitz[i]=0;
                    count--;
                }
            }
        }
        else if ( count<limitlevels )
        {
            //too few levels, add one uninteresting level after each uninteresting
            int last=0;
            uint32_t oldcount = count-1;
            while ( count<limitlevels&&oldcount<count )
            {
                oldcount = count;
                for ( int i=z_max;i>=0&&count<limitlevels;i-- )
                {
                    if ( limitz[i]==0 && last==255 )
                    {
                        limitz[i]=255;
                        count++;
                        last=0;
                    }
                    else
                    {
                        last=limitz[i];
                    }
                }
            }
            //if we are still low, you get extra air
        }
        cloudheight=limitairtokeep/2;
    }
    else
    {

        memset ( &limitz,255,1000 );
        limitlevels = z_max;
        cloudheight=5;
    }


    //setup variables and get data from Dwarf Fortress
    int dfxsquares = ( x_max-xoffset ) *SQUARESPERBLOCK ;
    int dfysquares = ( y_max-yoffset ) *SQUARESPERBLOCK;
    int dfzsquares = ( limitlevels );
    int mcxsquares = dfxsquares * squaresize;
    int mcysquares = dfysquares * squaresize;
    int mczsquares = dfzsquares * squaresize+1;

    if ( z_max>limitlevels )
    {
        out.print ( "DF Map size cut down to %d, %d, %d squares\n",dfxsquares,dfysquares, dfzsquares );
    }
    if ( mczsquares<128 ) mczsquares = 128;//this should be the case (<128) most of the time
    out.print ( "MC Map size will be %d, %d, %d squares\n",mcxsquares,mcysquares, mczsquares );
    out.print ( "plus surrounding random terrain using seed: %I64d\n",seed );

    if ( mcxsquares<0 || mcysquares<0 || mczsquares<0 )
    {
        out.printerr ( "Area too small\n" );
        return 20;
    }

    // get region geology
    if ( !Maps::ReadGeology ( layerassign ) )
    {
        out.printerr ("Can't get region geology.\n");
        return 106;
    }


    out.print ( "\nReading Plants... " );
    uint32_t numVegs = Vegetation::getCount();

    //read vegetation into a map for faster access later
    map<uint32_t,std::string> vegs;
    for ( uint32_t i =0; i < numVegs; i++ )
    {
        df::plant * p = Vegetation::getPlant(i);
        vegs[getMapIndex ( p->pos.x,p->pos.y,p->pos.z ) ] = df::global::world->raws.plants.all[p->material]->id;
    }
    out.print ( "%d\n",vegs.size() );



    //read buildings into a map organized by location (10 bit each for x,y,z packed into an int)
    out.print ( "Reading Buildings... " );
    map <uint32_t, string> custom_workshop_types;
    uint32_t numBuildings = 0;
    //DFHack::Position * Pos = DF->getPosition();

    map<uint32_t,myBuilding> Buildings;
    //FIXME: this is so totally different it's not even funny. Disabled for now.
    /*
    numBuildings = Buildings::getNumBuildings();
    if ( numBuildings )
    {
        Buildings::ReadCustomWorkshopTypes ( custom_workshop_types );

        for ( uint32_t i = 0; i < numBuildings; i++ )
        {
            DFHack::Buildings::t_building temp;
            Buildings::Read ( i, temp );
            std::string typestr;
            mem->resolveClassIDToClassname ( temp.type, typestr );
            //DFConsole->print("Address 0x%x, type %d (%s), %d/%d to %d/%d on level %d\n",temp.origin, temp.type, typestr.c_str(), temp.x1,temp.y1,temp.x2,temp.y2,temp.z);
            //DFConsole->print("Material %d %d\n", temp.material.type, temp.material.index);
            int32_t custom = temp.custom_type;
            if ( custom != -1 )
            {
                //DFConsole->print("Custom workshop type %s (%d)\n",custom_workshop_types[custom].c_str(),custom);
                typestr = custom_workshop_types[custom];
            }

            char tempstr[256];
            if ( typestr.find ( "building_" ) ==0 )
            {
                strncpy ( tempstr,typestr.c_str() +9,255 );
                tempstr[255]='\0';
            }
            else
            {
                strncpy ( tempstr,typestr.c_str(),255 );
                tempstr[255]='\0';
            }

            //remove ending 'st' if it exists
            int len=strlen ( tempstr );
            if ( len>2 && tempstr[len-1]=='t' && tempstr[len-2]=='s' )
            {
                tempstr[len-2]='\0';
            }

            for ( uint32_t x=temp.x1;x<=temp.x2;x++ )
            {
                for ( uint32_t y=temp.y1;y<=temp.y2;y++ )
                {
                    myBuilding *mb = new myBuilding;

                    strncpy ( mb->type,tempstr,255 );
                    mb->type[255]='\0';
                    mb->material.type = temp.material.type;
                    mb->material.index = temp.material.index;

                    if ( ( temp.x1==temp.x2 ) && ( temp.y1==temp.y2 ) )
                    {
                        snprintf ( mb->desc,15,"only" );
                    }
                    else if ( ( temp.x1==temp.x2 ) && ( y==temp.y2 ) )
                    {
                        snprintf ( mb->desc,15,"xonlyymax" );
                    }
                    else if ( ( temp.x1==temp.x2 ) && ( y<temp.y2 ) )
                    {
                        snprintf ( mb->desc,15,"xonlyy%d",y-temp.y1 );
                    }
                    else if ( ( x==temp.x2 ) && ( temp.y1==temp.y2 ) )
                    {
                        snprintf ( mb->desc,15,"xmaxyonly" );
                    }
                    else if ( ( x<=temp.x2 ) && ( temp.y1==temp.y2 ) )
                    {
                        snprintf ( mb->desc,15,"x%dyonly",x-temp.x1 );
                    }
                    else if ( ( x==temp.x2 ) && ( y==temp.y2 ) )
                    {
                        snprintf ( mb->desc,15,"xmaxymax" );
                    }
                    else if ( ( x==temp.x2 ) && ( y<temp.y2 ) )
                    {
                        snprintf ( mb->desc,15,"xmaxy%d",y-temp.y1 );
                    }
                    else if ( ( x<temp.x2 ) && ( y==temp.y2 ) )
                    {
                        snprintf ( mb->desc,15,"x%dymax",x-temp.x1 );
                    }
                    else
                    {
                        snprintf ( mb->desc,15,"x%dy%d",x-temp.x1,y-temp.y1 );
                    }
                    mb->desc[15]='\0';

                    uint32_t index = getMapIndex ( x,y,temp.z );

                    if ( strcmp ( mb->type,"stockpile" ) ==0 )
                    {
                        if ( Buildings.find ( index ) ==Buildings.end() )
                        {
                            Buildings[index] = *mb;
                            //                      }else{
                            //                          DFConsole->print("Not replacing a building with a stockpile\n");
                        }
                    }
                    else
                    {
                        Buildings.erase ( index );
                        Buildings[index] = *mb;
                    }
                }
            }

        }
    }
    DFConsole->print ( "%d\n",Buildings.size() );
    */


    //Constructions
    out.print ( "Reading Constructions... " );
    uint32_t numConstr = Constructions::getCount();
    map<uint32_t,myConstruction> Constructions;
    myConstruction *consmats = new myConstruction[numConstr];

    for ( uint32_t i = 0; i < numConstr; i++ )
    {
        df::construction * con = Constructions::getConstruction ( i );

        consmats[i].mat_type = con->mat_type;
        consmats[i].mat_idx = con->mat_index;
        consmats[i].form = con->item_type;

        uint32_t index = getMapIndex ( con->pos.x,con->pos.y,con->pos.z );
        Constructions[index] = consmats[i];
    }
    out.print ( "%d\n",Constructions.size() );



    //allocate memory for blocks and data
    uint8_t* mclayers = new uint8_t[mcxsquares*mcysquares*mczsquares];
    uint8_t* mcdata = new uint8_t[mcxsquares*mcysquares*mczsquares];
    uint8_t* mcskylight = new uint8_t[mcxsquares*mcysquares*mczsquares];
    uint8_t* mcblocklight = new uint8_t[mcxsquares*mcysquares*mczsquares];
    if ( mclayers==NULL || mcdata == NULL || mcskylight == NULL || mcblocklight == NULL )
    {
        out.printerr ( "Unable to allocate memory to store level data, exiting." );
        return 10;
    }
    memset ( mclayers,0,sizeof ( uint8_t ) * ( mcxsquares*mcysquares*mczsquares ) );
    memset ( mcdata,0,sizeof ( uint8_t ) * ( mcxsquares*mcysquares*mczsquares ) );
    memset ( mcskylight,0,sizeof ( uint8_t ) * ( mcxsquares*mcysquares*mczsquares ) );
    memset ( mcblocklight,0,sizeof ( uint8_t ) * ( mcxsquares*mcysquares*mczsquares ) );



    //read DF map data and create MC map blocks and data arrays;
    out.print ( "\nConverting Map...\n" );

    // walk the DF map!
    uint32_t zcount = 0;
    for ( uint32_t zzz = 0; zzz< z_max;zzz++ )
    {
        if ( limitz[zzz]==0 )
            continue;
        out.print ( "Layer %d/%d\t(%d/%d)\n",zzz,z_max,zcount,limitlevels );
        for ( uint32_t dfblockx = xoffset; dfblockx< x_max;dfblockx++ )
        {
            for ( uint32_t dfblocky = yoffset; dfblocky< y_max;dfblocky++ )
            {

                if ( Maps::getBlock ( dfblockx,dfblocky,zzz ) )
                {

                    convertDFBlock ( out, layerassign, Constructions, Buildings, vegs,
                                     uio, mclayers,mcdata,
                                     dfblockx, dfblocky, zzz, zcount, xoffset, yoffset, mcxsquares, mcysquares );

                }

            }
        }

        //print stats
        for ( int i=0;i<STAT_AREAS;i++ )
        {
            switch ( i )
            {
            case MATERIALS:
                out.print ( " MATERIALS:\t" );
                break;
            case TERRAIN:
                out.print ( " TERRAIN:  \t" );
                break;
            case FLOWS:
                out.print ( " FLOWS:    \t" );
                break;
            case PLANTS:
                out.print ( " PLANTS:   \t" );
                break;
            case BUILDINGS:
                out.print ( " BUILDINGS:\t" );
                break;
            }
            out.print ( "unknown: %d  imperfect: %d  perfect: %d  new: %d\n",stats[i][UNKNOWN],stats[i][IMPERFECT],stats[i][PERFECT],stats[i][UNSEEN] );
        }

        zcount++;
    }

    if ( uio!=NULL )
    {
        if ( uio->GetDocument() !=NULL )
        {
            uio->GetDocument()->SaveFile();
            delete uio->GetDocument();
        }
    }


    //add bottom layer of adminium
    for ( int oy=0;oy<mcxsquares;oy++ )
    {
        for ( int ox=0;ox<mcysquares;ox++ )
        {

            //Index = x + (y * Depth + z) * Width //where y is up down
            int idx = ox + ( oy ) * mcxsquares;
            mclayers[idx]=7; // bedrock / adminium
        }
    }


    //place the spawn at DF cursor location, if within output area and not a wall
    out.print ( "\nPlancing spawn location\n" );
    int32_t cx, cy, cz,ocx,ocy,ocz;
    Gui::getCursorCoords ( ocx,ocy,ocz );
    cx=ocx;
    cy=ocy;
    cz=ocz;
    if ( cx != -30000 && cx>=0 && cy>=0 && cz>=0 )
    {
        //see if it is in an output layer (should be if a open space was chosen)
        if ( limitz[cz]=255 )
        {
            if ( ( ( limitxmin*SQUARESPERBLOCK ) <= ( uint32_t ) cx ) && ( ( limitxmax*SQUARESPERBLOCK ) >= ( uint32_t ) cx ) &&
                    ( ( limitymin*SQUARESPERBLOCK ) <= ( uint32_t ) cy ) && ( ( limitymax*SQUARESPERBLOCK ) >= ( uint32_t ) cy ) )
            {

                //convert to MC index - need to rotate - need to position from NW with +x going south and +y going west (I think)
                bool ok = false;
                int32_t temp = cx;
                cx = cy * squaresize - ( yoffset*SQUARESPERBLOCK*squaresize ) + ( squaresize/2 );
                cy = mcxsquares - ( ( temp * squaresize - ( xoffset*SQUARESPERBLOCK*squaresize ) ) + ( squaresize/2 ) );
                int zcnt = 0; //we need to count the number of included levels from here to the bottom as that is the actual layer height in minecraft
                for ( int k=cz-1;k>=0;k-- )
                {
                    if ( limitz[k]>0 )
                    {
                        zcnt++;
                    }
                }
                //now check if that location is ok, or do we have to move up because it is a floor cube.
                for ( int i=1;i< ( squaresize ) &&!ok;i++ )  //starting at 1 because of the one layer of adminium at the bottom of the map
                {
                    cz = zcnt * squaresize + i;
                    int idx = cx + ( cz * mcysquares +cy ) * mcxsquares;
                    int idx1 = cx + ( ( cz+1 ) * mcysquares +cy ) * mcxsquares;

                    if ( mclayers[idx]==0 && mclayers[idx1]==0 )
                    {
                        //if this location and one above it are air, spawn location is ok
                        ok = true;
                    }
                }
                if ( !ok )
                {
                    out.print ( "DF Cursor not at location with enough space for spawn\nSetting spawn to top center of area\n" );
                    cx = -1;//never found a valid location - place in center
                }
            }
            else
            {
                out.print ( "DF Cursor outside of area output on this level\nSetting spawn to top center of area\n" );
                cx=-1;
            }
        }
        else
        {
            out.print ( "DF Cursor on level not being output\nSetting spawn to top center of area\n" );
            cx=-1;
        }
    }
    if ( cx==30000 || cx<0 )
    {
        //need to set spawn to center of map;
        out.print ( "Putting spawn at center of map...\n" );
        cx = mcxsquares/2;
        cy = mcysquares/2;
        cz = mczsquares;
        uint8_t val = 0;
        int pos;
        while ( val == 0 && cz>0 )
        {
            cz--;
            pos = ( cx ) + ( ( cz ) * mcysquares + cy ) * mcxsquares;
            val = mclayers[pos];
        };
        cz++;
        ocx = ( ( mcysquares - cy ) / squaresize + ( xoffset*SQUARESPERBLOCK ) );
        ocy = cx / squaresize + ( yoffset*SQUARESPERBLOCK );
        ocz = cz / squaresize;
        int cnt=0;//find the (cz / squaresize)th level set for output - that level is the level in DF
        for ( int i=0;i<999;i++ )
        {
            if ( limitz[i]>0 )
            {
                if ( cnt== ( cz / squaresize ) )
                {
                    ocz=i;
                }
                cnt++;
            }
        }

    }
    out.print ( "Putting spawn at %d,%d,%d in Minecraft\nwhich is at %d,%d,%d in Dwarf Fortress\n",cx,cy,cz,ocx,ocy,ocz );

    calcLighting ( out, mclayers,mcskylight,mcblocklight, mcxsquares, mcysquares, mczsquares );

    //save the level!
    return saveMCLevelAlpha ( out, mclayers, mcdata, mcskylight, mcblocklight, mcxsquares, mcysquares, mczsquares,cx,cy,cz,NULL );
}

/*
//this was used to generate the directional walls in the settings file
void wallDirection(){

for(int i=0;i<256;i++){

char temp[16];
char dir[256];
char mat[1024];
dir[0]='\0';
mat[0]='\0';

//get direction string;
for(int j=0;j<8;j++){
if((i>>j)&1){
temp[0] = '1'+((j>3)?j+1:j);
temp[1] = '\0';
strcat(dir,temp);
}
}

//get material string
for(int j=0;j<2;j++){//setup wall for top ande middle layer
if(!(i&2) && !(i&8) && (i&1)){
strcat(mat,"0");
}else{
strcat(mat,"1");
}
strcat(mat,",1,");
if(!(i&2) && !(i&16) && (i&4)){
strcat(mat,"0");
}else{
strcat(mat,"1");
}

strcat(mat,";1,1,1;");

if(!(i&64) && !(i&8) && (i&32)){
strcat(mat,"0");
}else{
strcat(mat,"1");
}
strcat(mat,",1,");
if(!(i&64) && !(i&16) && (i&128)){
strcat(mat,"0");
}else{
strcat(mat,"1");
}

strcat(mat,"|");
}

strcat(mat,"1,1,1;1,1,1;1,1,1");

DFConsole->print("<wall%s mat=\"%s\" />\n",dir,mat);
}

DFConsole->print("\nPress enter key");
cin.ignore();

}
*/

DFHACK_PLUGIN("df2minecraft");

DFhackCExport command_result mc_export (DFHack::color_ostream & c, vector <string> & parameters);

DFhackCExport command_result plugin_init (DFHack::color_ostream & c, std::vector <PluginCommand> &commands)
{
    commands.push_back(PluginCommand("df2minecraft", "Print the weather map or change weather.",mc_export));
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown ( DFHack::color_ostream & c )
{
    return CR_OK;
}

DFhackCExport command_result mc_export (DFHack::color_ostream & c, vector <string> & parameters)
{
    //load settings xml
    TiXmlDocument doc ( "hack/df2mc.xml" );
    bool loadOkay = doc.LoadFile();
    if ( !loadOkay )
    {
        c.printerr ( "Could not load hack/df2mc.xml\n" );
        return CR_FAILURE;
    }

    //load basic settings
    settings = doc.FirstChildElement ( "settings" );
    if ( settings==NULL )
    {
        c.printerr ( "Could not load settings from hack/df2mc.xml (corrupt xml file?)\n" );
        return CR_FAILURE;
    }
    squaresize=3;
    if ( settings->FirstChildElement ( "squaresize" ) ==NULL )
    {
        TiXmlElement * ss = new TiXmlElement ( "squaresize" );
        ss->SetAttribute ( "val",3 );
        settings->LinkEndChild ( ss );
    }
    const char* res = settings->FirstChildElement ( "squaresize" )->Attribute ( "val",&squaresize );
    if ( res==NULL || squaresize<1 || squaresize>10 )
    {
        c.printerr ( "Invalid square size setting\n" );
        return CR_FAILURE;
    }


    if ( settings->FirstChildElement ( "torchinsidepercent" ) ==NULL )
    {
        TiXmlElement * ss = new TiXmlElement ( "torchinsidepercent" );
        settings->LinkEndChild ( ss );
    }
    if ( settings->FirstChildElement ( "torchinsidepercent" )->Attribute ( "val",&torchPerInside ) ==NULL )
    {
        torchPerInside = 10;
        settings->FirstChildElement ( "torchinsidepercent" )->SetAttribute ( "val",torchPerInside );
    }
    if ( settings->FirstChildElement ( "torchdarkpercent" ) ==NULL )
    {
        TiXmlElement * ss = new TiXmlElement ( "torchdarkpercent" );
        settings->LinkEndChild ( ss );
    }
    if ( settings->FirstChildElement ( "torchdarkpercent" )->Attribute ( "val",&torchPerDark ) ==NULL )
    {
        torchPerDark=20;
        settings->FirstChildElement ( "torchdarkpercent" )->SetAttribute ( "val",torchPerDark );
    }
    if ( settings->FirstChildElement ( "torchsubterraneanpercent" ) ==NULL )
    {
        TiXmlElement * ss = new TiXmlElement ( "torchsubterraneanpercent" );
        settings->LinkEndChild ( ss );
    }
    if ( settings->FirstChildElement ( "torchsubterraneanpercent" )->Attribute ( "val",&torchPerSubter ) ==NULL )
    {
        torchPerSubter=30;
        settings->FirstChildElement ( "torchsubterraneanpercent" )->SetAttribute ( "val",torchPerSubter );
    }

    if ( settings->FirstChildElement ( "output" ) ==NULL )
    {
        TiXmlElement * ss = new TiXmlElement ( "output" );
        settings->LinkEndChild ( ss );
    }
    if ( settings->FirstChildElement ( "output" )->Attribute ( "type" ) ==NULL )
    {
        settings->FirstChildElement ( "output" )->SetAttribute ( "type","alpha" );
    }
    if ( settings->FirstChildElement ( "output" )->Attribute ( "seed" ) ==NULL )
    {
        settings->FirstChildElement ( "output" )->SetAttribute ( "seed","" );
    }

    int64_t tmpseed = _atoi64 ( settings->FirstChildElement ( "output" )->Attribute ( "seed" ) );
    if ( tmpseed != 0 )
    {
        seed = tmpseed;
    }

    if ( settings->FirstChildElement ( "snowy" ) ==NULL )
    {
        TiXmlElement * ss = new TiXmlElement ( "snowy" );
        settings->LinkEndChild ( ss );
    }
    if ( settings->FirstChildElement ( "snowy" )->Attribute ( "val", &snowy ) ==NULL )
    {
        snowy=0;
        settings->FirstChildElement ( "snowy" )->SetAttribute ( "val","0" );
    }

    if ( settings->FirstChildElement ( "directionalwalls" ) ==NULL )
    {
        TiXmlElement * ss = new TiXmlElement ( "directionalwalls" );
        settings->LinkEndChild ( ss );
    }
    if ( settings->FirstChildElement ( "directionalwalls" )->Attribute ( "val", &directionalWalls ) ==NULL )
    {
        directionalWalls=0;
        settings->FirstChildElement ( "directionalwalls" )->SetAttribute ( "val","0" );
    }

    if ( settings->FirstChildElement ( "safesand" ) ==NULL )
    {
        TiXmlElement * ss = new TiXmlElement ( "safesand" );
        settings->LinkEndChild ( ss );
    }
    if ( settings->FirstChildElement ( "safesand" )->Attribute ( "val", &safesand ) ==NULL )
    {
        safesand=3;
        settings->FirstChildElement ( "safesand" )->SetAttribute ( "val","3" );
    }

    int temp;
    limitxmin=0;
    limitymin=0;
    limitxmax=2000;
    limitymax=2000;
    if ( settings->FirstChildElement ( "verticalarea" ) !=NULL )
    {
        const char *str;
        if ( ( str=settings->FirstChildElement ( "verticalarea" )->Attribute ( "type" ) ) !=NULL )
        {
            if ( stricmp ( str,"top" ) ==0 )
            {
                limittype = LIMITTOP;
            }
            else if ( stricmp ( str,"range" ) ==0 )
            {
                limittype = LIMITRANGE;
            }
            else if ( stricmp ( str,"smart" ) ==0 )
            {
                limittype = LIMITSMART;
            }
            else
            {
                limittype = LIMITTOP;
            }
        }
        if ( settings->FirstChildElement ( "verticalarea" )->Attribute ( "levels",&temp ) !=NULL )
        {
            limitlevels = temp;
        }
        if ( settings->FirstChildElement ( "verticalarea" )->Attribute ( "airtokeep",&temp ) !=NULL )
        {
            limitairtokeep = temp;
        }
        if ( settings->FirstChildElement ( "verticalarea" )->Attribute ( "toplevel",&temp ) !=NULL )
        {
            limittoplevel = temp;
        }
    }

    if ( ( limitlevels*squaresize ) > (CHUNK_HEIGHT-1) )
    {
        limitlevels = (CHUNK_HEIGHT-1)/squaresize;
    }

    //load MC material mappings
    mcMats.clear();
    //  dfMat2mcMat.clear();
    loadMcMats ( &doc, c );


    //load objects
    xmlmaterials = doc.FirstChildElement ( "dwarffortressmaterials" );
    if ( xmlmaterials == NULL )
    {
        xmlmaterials = new TiXmlElement ( "dwarffortressmaterials" );
        doc.LinkEndChild ( xmlmaterials );
        TiXmlComment *comment = new TiXmlComment ( "This is what a solid block of a particular material in Dwarf Fortress should look like in Minecraft. These blocks will then be carved into the various terrain objects. \nEach object has a list of mats that is squaresize^3 in size (separated by ',',';', or '|') that is ordered left to right, back to front, top to bottom (i.e. the first items listed move across a row, then the next row in that layer comes, and when one layer is finished, the next layer down is started)\n These should only be basic blocks, and so no data array is needed." );
        xmlmaterials->LinkEndChild ( comment );
    }
    xmlterrain = doc.FirstChildElement ( "terrain" );
    if ( xmlterrain == NULL )
    {
        xmlterrain = new TiXmlElement ( "terrain" );
        doc.LinkEndChild ( xmlterrain );
        TiXmlComment *comment = new TiXmlComment ( "These are the terrain types we will convert from Dwarf Fortress and how we will represent them in Minecraft\nEach object has a list of mats that is squaresize^3 in size (separated by ',',';', or '|') that is ordered left to right, back to front, top to bottom (i.e. the first items listed move across a row, then the next row in that layer comes, and when one layer is finished, the next layer down is started).\nIf we should use the block type from the material, use -1, for that location should be empty (air), you can pust 0 (or air).\nEach object also has a list of numbers, the same squaresize^3 in size, that represent state or data of the cooresponding material, used to control things like facing direction, etc." );
        xmlterrain->LinkEndChild ( comment );
    }
    xmlflows = doc.FirstChildElement ( "flows" );
    if ( xmlflows == NULL )
    {
        xmlflows = new TiXmlElement ( "flows" );
        doc.LinkEndChild ( xmlflows );
        TiXmlComment *comment = new TiXmlComment ( "These are the flow types we will convert from Dwarf Fortress and how we will represent them in Minecraft\nEach object has a list of mats that is squaresize^3 in size (separated by ',',';', or '|') that is ordered left to right, back to front, top to bottom (i.e. the first items listed move across a row, then the next row in that layer comes, and when one layer is finished, the next layer down is started).\nIf we should use the block type from the material, use -1, for that location should be empty (air), you can pust 0 (or air).\nEach object also has a list of numbers, the same squaresize^3 in size, that represent state or data of the cooresponding material, used to control things like facing direction, etc." );
        xmlflows->LinkEndChild ( comment );
    }
    xmlplants = doc.FirstChildElement ( "plants" );
    if ( xmlplants == NULL )
    {
        xmlplants = new TiXmlElement ( "plants" );
        doc.LinkEndChild ( xmlplants );
        TiXmlComment *comment = new TiXmlComment ( "These are the plant types we will convert from Dwarf Fortress and how we will represent them in Minecraft\nEach object has a list of mats that is squaresize^3 in size (separated by ',',';', or '|') that is ordered left to right, back to front, top to bottom (i.e. the first items listed move across a row, then the next row in that layer comes, and when one layer is finished, the next layer down is started).\nIf we should use the block type from the material, use -1, for that location should be empty (air), you can pust 0 (or air).\nEach object also has a list of numbers, the same squaresize^3 in size, that represent state or data of the cooresponding material, used to control things like facing direction, etc." );
        xmlplants->LinkEndChild ( comment );
    }
    xmlbuildings = doc.FirstChildElement ( "buildings" );
    if ( xmlbuildings == NULL )
    {
        xmlbuildings = new TiXmlElement ( "buildings" );
        doc.LinkEndChild ( xmlbuildings );
        TiXmlComment *comment = new TiXmlComment ( "These are the building types we will convert from Dwarf Fortress and how we will represent them in Minecraft\nEach object has a list of mats that is squaresize^3 in size (separated by ',',';', or '|') that is ordered left to right, back to front, top to bottom (i.e. the first items listed move across a row, then the next row in that layer comes, and when one layer is finished, the next layer down is started).\nIf we should use the block type from the material, use -1, for that location should be empty (air), you can pust 0 (or air).\nEach object also has a list of numbers, the same squaresize^3 in size, that represent state or data of the cooresponding material, used to control things like facing direction, etc." );
        xmlbuildings->LinkEndChild ( comment );
    }

    loadDFObjects(c);

    //convert the map
    int result = convertMaps ( c );

    doc.SaveFile ( "hack/updated.xml" );

    if (result)
        return CR_FAILURE;
    else
        return CR_OK;
}
