/*
tm_polygon.h v1.0c - public domain
author: Tolga Mizrak 2016

no warranty; use at your own risk

USAGE
	This file works as both the header and implementation.
	To implement the interfaces in this header,
		#define TM_POLYGON_IMPLEMENTATION
	in ONE C or C++ source file before #including this header.

NOTES
	This library implements algorithms to triangulate polygons and clip polygons against one
	another.
	Note that the algorithms do not allocate any memory on their own. When calling the functions,
	you have to pass in the memory the algorithms operate on, so be sure to be generous with memory,
	or the algorithms can miss intersections or generate too few polygons in case of the clipping
	functions.

	The triangulation is implemented using ear clipping and does not handle self intersecting
	polygons or polygons with holes. It takes a list of vertices and emits triangles in the form of
	indices. These indices can be used to populate a gpu index buffer directly.

	Clipping polygons is implemented using the Greiner–Hormann clipping algorithm.
	Clipping is done in 3 phases, with one additional initialization step.
	Call these functions in this order:
	First you need to convert your polygon into a format that can be used by the algorithm.
	This is done using the function clipPolyTransformData, it returns the polygon in a format that
	the algorithm can work with. The function expects you to pass in the memory for the intermediate
	polygon format. Be sure to pass in enough memory (in form of ClipVertex entries) so that the
	algorithm can store intersections in the same structures. Transform both your polygons like
	this.

	The first phase is finding intersections. Call into clipPolyFindIntersections with both
	polygons. The intersections will be stored in the polygons themselves, this is why you need to
	pass in enough memory into clipPolyTransformData.

	The second phase is clipPolyMarkEntryExitPoints. This marks whether the intersections are entry
	vertices into the other polygon or exit vertices. At the same time this function expects two
	ClipFollowDirection arguments, one for each polygon. What follow direction you want depends on
	the operation you want to do. Refer to the following table:
		Polygon a | Polygon b | Result
		Forward   | Forward   | a AND b (difference of a and b)
		Backward  | Forward   | a \ b
		Forward   | Backward  | b \ a
		Backward  | Backward  | a OR b (union of a and b)

	The third phase is clipPolyEmitClippedPolygons. Now we are ready to emit polygons. The emitted
	polygons depend on the ClipFollowDirection arguments to clipPolyMarkEntryExitPoints. Note that
	again you have to supply the memory for the polygons. All polygons share the same pool of
	vertices (arguments 5 and 6).

ISSUES
	Clipping:
		- in case one polygon is completely inside another, only in case of clipping (logical
		  AND of both polygons) the right result is being returned.

HISTORY
	v1.0c   07.10.16 changed tmp prefix to tmpo prefix, since tm_print.h already uses tmp
	                 removed using unsigned arithmetic when tmpo_size_t is signed
	v1.0b	02.07.16 changed #include <memory.h> into string.h
	v1.0a	01.07.16 improved C99 conformity
	v1.0	26.06.16 initial commit

LICENSE
	This software is dual-licensed to the public domain and under the following
	license: you are granted a perpetual, irrevocable license to copy, modify,
	publish, and distribute this file as you see fit.
*/

#ifdef TM_POLYGON_IMPLEMENTATION
// define these to avoid crt

	#ifndef TMPO_ASSERT
		#include <assert.h>
		#define TMPO_ASSERT assert
	#endif

	#ifndef TMPO_MEMMOVE
		#include <string.h>
		#define TMPO_MEMMOVE memmove
	#endif

	#ifndef TMPO_MEMSET
		#include <string.h>
		#define TMPO_MEMSET memset
	#endif
#endif

#ifndef _TM_POLYGON_H_INCLUDED_
#define _TM_POLYGON_H_INCLUDED_

#ifndef TMPO_OWN_TYPES
	typedef unsigned short tmpo_index;
	typedef size_t tmpo_size_t;
	typedef unsigned short tmpo_uint16;
	#ifdef __cplusplus
		typedef bool tmpo_bool;
	#else
		typedef int tmpo_bool;
	#endif
#endif

#ifdef __cplusplus
	#define TMPO_UNDERLYING : tmpo_uint16
#else
	#define TMPO_UNDERLYING
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TMPO_VECTOR
	typedef struct {
		float x;
		float y;
	} tmpo_vector;
	#define TMPO_VECTOR tmpo_vector
	// if your vector struct is too big for value copies, #define this with const your_vector&
	#define TMPO_VECTOR_CONST_REF tmpo_vector
#endif


#ifndef TMPO_CLOCKWISE_TRIANGLES
	// define this to 0 if you want anti clockwise triangles to be generated
	#define TMPO_CLOCKWISE_TRIANGLES 1
#endif

#ifndef TMPO_STATIC
	#define TMPO_DEF extern
#else
	#define TMPO_DEF static
#endif

TMPO_DEF tmpo_bool isPolygonClockwise( const TMPO_VECTOR* vertices, tmpo_size_t count );

// takes a polygon and outputs a stream of indices that define triangles
// Args:
//		vertices: vertices that the polygon is made out of
//		count: how many vertices there are
//		clockwise: whether the polygon is oriented clockwise
//		queryList: internal list the algorithm needs to triangulate. Make sure it is as big as count
//		queryCount: the size of the queryList. Make sure it is as big as count
//		begin: offset to the indices
//		out: the resulting indices will be placed here. Make sure that it is at least 3 * count.
//		maxIndices: size of the array in out
// returns the number of indices generated
TMPO_DEF tmpo_size_t triangulatePolygonEarClipping( const TMPO_VECTOR* vertices, tmpo_size_t count,
                                                    tmpo_bool clockwise, tmpo_index* queryList,
                                                    tmpo_size_t queryCount, tmpo_index begin,
                                                    tmpo_index* out, tmpo_size_t maxIndices );

// clipping using Greiner–Hormann clipping algorithm
typedef struct {
	TMPO_VECTOR pos;
	tmpo_uint16 next;
	tmpo_uint16 prev;
	tmpo_uint16 nextPoly;
	tmpo_uint16 flags;
	tmpo_uint16 neighbor;
	float alpha;
} ClipVertex;

enum ClipVertexFlags TMPO_UNDERLYING {
	CVF_INTERSECT = ( 1 << 0 ),
	CVF_EXIT      = ( 1 << 1 ),
	CVF_PROCESSED = ( 1 << 2 ),

	CVF_FORCE_16 = 0x7FFF
};

typedef enum { CFD_FORWARD, CFD_BACKWARD } ClipFollowDirection;

typedef struct {
	ClipVertex* data;
	tmpo_size_t originalSize;
	tmpo_size_t size;
	tmpo_size_t capacity;
} ClipVertices;

typedef struct {
	TMPO_VECTOR* vertices;
	tmpo_size_t size;
} ClipPolygonEntry;

typedef struct {
	tmpo_size_t polygons;  // how many polygons we emitted
	tmpo_size_t vertices;  // how many vertices were used up for the polygons in total
} ClipPolyResult;

// converts an array of vertices into a format usable by the clipping algorithm
// note that you supply the memory for the algorithm to use in the 3rd and 4th argument
// make sure that you supply enough clipVertices so that intersections can be stored
TMPO_DEF ClipVertices clipPolyTransformData( const TMPO_VECTOR* vertices, tmpo_size_t count,
                                             ClipVertex* clipVertices,
                                             tmpo_size_t maxClipVertices );

// phase 1 of the clipping algorithm
TMPO_DEF void clipPolyFindIntersections( ClipVertices* a, ClipVertices* b );

// phase 2 of the clipping algorithm
// the ClipFollowDirection values depend on what operation you want to do:
//		Polygon a    | Polygon b    | Result
//		CFD_FORWARD  | CFD_FORWARD  | a AND b (difference of a and b)
//		CFD_BACKWARD | CFD_FORWARD  | a \ b
//		CFD_FORWARD  | CFD_BACKWARD | b \ a
//		CFD_BACKWARD | CFD_BACKWARD | a OR b (union of a and b)
TMPO_DEF void clipPolyMarkEntryExitPoints( ClipVertices* a, ClipVertices* b,
                                           ClipFollowDirection aDir, ClipFollowDirection bDir );

// phase 3 of the clipping algorithm
// the clipped polygons will be emitted. The memory for the polygons are passed in through the
// arguments 3 through 6. All polygons share the same vertices pool (arguments 5 and 6).
// returns a structure containing the number of polygons emitted and the total number of vertices
// consumed. The number of vertices that each polygon takes up is stored directly in the third
// argument (polygons)
TMPO_DEF ClipPolyResult clipPolyEmitClippedPolygons( ClipVertices* a, ClipVertices* b,
                                                     ClipPolygonEntry* polygons,
                                                     tmpo_size_t maxPolygons, TMPO_VECTOR* vertices,
                                                     tmpo_size_t maxCount );

// convenience function in case you only expect one polygon to be emitted
TMPO_DEF tmpo_size_t clipPolyEmitClippedPolygon( ClipVertices* a, ClipVertices* b,
                                                 TMPO_VECTOR* vertices, tmpo_size_t maxCount );

#ifdef __cplusplus
}
#endif

#endif  // _TM_POLYGON_H_INCLUDED_

// implementation

#ifdef TM_POLYGON_IMPLEMENTATION

#ifdef __cplusplus
extern "C" {
#endif

TMPO_DEF tmpo_bool isPolygonClockwise( const TMPO_VECTOR* vertices, tmpo_size_t count )
{
	float sum        = 0;
	tmpo_size_t last = count - 1;
	for( tmpo_size_t i = 0; i < count; last = i, ++i ) {
		sum += vertices[last].x * vertices[i].y - vertices[last].y * vertices[i].x;
	}
	return sum >= 0;
}

static int isTriangleClockwise( TMPO_VECTOR_CONST_REF a, TMPO_VECTOR_CONST_REF b,
                                TMPO_VECTOR_CONST_REF c )
{
	float bx = b.x - a.x;
	float by = b.y - a.y;
	float cx = c.x - a.x;
	float cy = c.y - a.y;

	float cross = bx * cy - by * cx;
	return cross >= 0;
}
static int pointInsideTriangle( TMPO_VECTOR_CONST_REF a, TMPO_VECTOR_CONST_REF b,
                                TMPO_VECTOR_CONST_REF c, TMPO_VECTOR_CONST_REF v )
{
	float bx = b.x - a.x;
	float by = b.y - a.y;
	float cx = c.x - a.x;
	float cy = c.y - a.y;
	float vx = v.x - a.x;
	float vy = v.y - a.y;

	float bc = bx * cx + by * cy;
	float vc = vx * cx + vy * cy;
	float vb = vx * bx + vy * by;
	float cc = cx * cx + cy * cy;
	float bb = bx * bx + by * by;

	float invDenom = 1.0f / ( bb * cc - bc * bc );
	float r        = ( cc * vb - bc * vc ) * invDenom;
	float s        = ( bb * vc - bc * vb ) * invDenom;

	return ( r >= 0 ) && ( s >= 0 ) && ( r + s <= 1 );
}
static int isTriangleEar( tmpo_size_t a, tmpo_size_t b, tmpo_size_t c, const TMPO_VECTOR* vertices,
                          tmpo_size_t count, int cw )
{
	TMPO_ASSERT( a < count );
	TMPO_ASSERT( b < count );
	TMPO_ASSERT( c < count );

	TMPO_VECTOR va = vertices[a];
	TMPO_VECTOR vb = vertices[b];
	TMPO_VECTOR vc = vertices[c];
	if( isTriangleClockwise( va, vb, vc ) != cw ) {
		return 0;
	}

	for( tmpo_size_t i = 0; i < count; ++i ) {
		if( i == a || i == b || i == c ) {
			continue;
		}
		if( pointInsideTriangle( va, vb, vc, vertices[i] ) ) {
			return 0;
		}
	}

	return 1;
}

TMPO_DEF tmpo_size_t triangulatePolygonEarClipping( const TMPO_VECTOR* vertices, tmpo_size_t count,
                                                    tmpo_bool clockwise, tmpo_index* queryList,
                                                    tmpo_size_t queryCount, tmpo_index begin,
                                                    tmpo_index* out, tmpo_size_t maxIndices )
{
	TMPO_ASSERT( vertices );
	TMPO_ASSERT( count >= 0 );
	TMPO_ASSERT( queryList );
	TMPO_ASSERT( queryCount >= count );
	TMPO_ASSERT( out );
	TMPO_ASSERT( maxIndices );

	if( count < 3 ) {
		return 0;
	}

	// int clockwise = isTriangleClockwise( vertices[0], vertices[1], vertices[2] );
	tmpo_size_t verticesCount = count;

	tmpo_size_t size = ( count < queryCount ) ? ( count ) : queryCount;
	for( tmpo_size_t i = 0; i < size; ++i ) {
		queryList[i] = (tmpo_index)i;
	}

	tmpo_size_t indicesCount = 0;
	tmpo_size_t a = 0, b = 1, c = 2;
	tmpo_size_t current        = 2;
	tmpo_size_t iterationCount = 0;
	while( size > 2 ) {
		if( isTriangleEar( queryList[a], queryList[b], queryList[c], vertices, verticesCount,
		                   clockwise ) ) {
			if( indicesCount + 3 > maxIndices ) {
				TMPO_ASSERT( 0 && "out of memory" );
				break;
			}
			if( clockwise == TMPO_CLOCKWISE_TRIANGLES ) {
				*( out++ ) = queryList[a] + begin;
				*( out++ ) = queryList[b] + begin;
				*( out++ ) = queryList[c] + begin;
			} else {
				*( out++ ) = queryList[a] + begin;
				*( out++ ) = queryList[c] + begin;
				*( out++ ) = queryList[b] + begin;
			}
			indicesCount += 3;

			--size;
			iterationCount = 0;
			TMPO_MEMMOVE( queryList + b, queryList + b + 1, ( size - b ) * sizeof( tmpo_index ) );
			current = a;
			if( current >= size ) {
				current = current - size;
			}
			if( current >= 2 ) {
				a = current - 2;
				b = current - 1;
			} else {
				a = size - ( 2 - current );

				if( current >= 1 ) {
					b = current - 1;
				} else {
					b = size - ( 1 - current );
				}
			}
			c = current;
		} else {
			a = b;
			b = current;
			++current;
			c = current;
			if( current >= size ) {
				current = 0;
				a       = size - 2;
				b       = size - 1;
				c       = current;
			}
			if( iterationCount > 2 * size ) {
				break;
			}
			++iterationCount;
		}
	}

	return (tmpo_size_t)indicesCount;
}

// clipPoly implementation

TMPO_DEF ClipVertices clipPolyTransformData( const TMPO_VECTOR* vertices, tmpo_size_t count,
                                             ClipVertex* clipVertices, tmpo_size_t maxClipVertices )
{
	TMPO_ASSERT( maxClipVertices >= count );

	ClipVertices result = {clipVertices, count, count, maxClipVertices};

	tmpo_size_t verticesCount = count;
	TMPO_MEMSET( clipVertices, 0, verticesCount * sizeof( ClipVertex ) );
	tmpo_index prev = (tmpo_index)verticesCount - 1;
	for( tmpo_size_t i = 0; i < verticesCount; ++i ) {
		ClipVertex* entry = &clipVertices[i];
		entry->pos        = vertices[i];
		entry->next       = ( tmpo_index )( i + 1 );
		entry->prev       = prev;
		prev              = (tmpo_index)i;
	}
	if( verticesCount ) {
		clipVertices[verticesCount - 1].next = 0;
	}
	return result;
}

// helpers
static ClipVertex* clipPolyCreateVertex( ClipVertices* vertices, tmpo_size_t at )
{
	// contrary to how insertion works in linked lists usually, we insert AFTER at, not before
	// this safes us some checks because of the way we iterate over vertices

	TMPO_ASSERT( vertices->size + 1 <= vertices->capacity );
	ClipVertex* added   = &vertices->data[vertices->size];
	ClipVertex* ref     = &vertices->data[at];
	ClipVertex* oldNext = &vertices->data[ref->next];
	added->prev         = (tmpo_index)at;
	added->next         = ref->next;
	added->flags        = 0;
	oldNext->prev = ref->next = (tmpo_index)vertices->size;
	++vertices->size;
	return added;
}
static int clipPolyLineIntersectionFactor( TMPO_VECTOR_CONST_REF a, TMPO_VECTOR_CONST_REF aDir,
                                           TMPO_VECTOR_CONST_REF b, TMPO_VECTOR_CONST_REF bDir,
                                           float* t )
{
	TMPO_ASSERT( t );

	int hit     = 0;
	float cross = aDir.x * bDir.y - aDir.y * bDir.x;
	if( cross < 0.000001f || cross > 0.000001f ) {
		float relx = a.x - b.x;
		float rely = a.y - b.y;
		*t         = ( bDir.x * rely - bDir.y * relx ) / cross;
		hit        = 1;
	}
	return hit;
}

static tmpo_size_t clipPolyFindIntersectionIndex( ClipVertices* vertices, tmpo_size_t at,
                                                  float alpha )
{
	ClipVertex* ref = &vertices->data[at];
	while( ( ref->flags & CVF_INTERSECT ) && ref->alpha > alpha ) {
		at  = ref->prev;
		ref = &vertices->data[at];
	}
	return at;
}
static void clipPolyCreateIntersection( ClipVertices* vertices, tmpo_size_t at,
                                        TMPO_VECTOR_CONST_REF intersection, tmpo_size_t neighbor,
                                        float alpha )
{
	ClipVertex* added = clipPolyCreateVertex( vertices, at );
	added->pos        = intersection;
	added->flags |= CVF_INTERSECT;
	added->neighbor = (tmpo_index)neighbor;
	added->alpha    = alpha;
}

TMPO_DEF void clipPolyFindIntersections( ClipVertices* a, ClipVertices* b )
{
	tmpo_size_t aCount     = a->originalSize;
	tmpo_size_t bCount     = b->originalSize;
	tmpo_size_t aPrevIndex = aCount - 1;
	tmpo_size_t bPrevIndex = bCount - 1;
	for( tmpo_size_t i = 0; i < aCount; aPrevIndex = i, ++i ) {
		for( tmpo_size_t j = 0; j < bCount; ) {
			ClipVertex* aVertex  = &a->data[i];
			TMPO_VECTOR aCurrent = aVertex->pos;
			TMPO_VECTOR aPrev    = a->data[aPrevIndex].pos;

			ClipVertex* bVertex  = &b->data[j];
			TMPO_VECTOR bCurrent = bVertex->pos;
			TMPO_VECTOR bPrev    = b->data[bPrevIndex].pos;

			TMPO_VECTOR aDir = {aCurrent.x - aPrev.x, aCurrent.y - aPrev.y};
			TMPO_VECTOR bDir = {bCurrent.x - bPrev.x, bCurrent.y - bPrev.y};

			float aAlpha, bAlpha;
			if( clipPolyLineIntersectionFactor( aPrev, aDir, bPrev, bDir, &aAlpha )
			    && clipPolyLineIntersectionFactor( bPrev, bDir, aPrev, aDir, &bAlpha )
			    && aAlpha >= 0.0f && aAlpha <= 1.0f && bAlpha >= 0.0f && bAlpha <= 1.0f ) {

				// check for degenerates, ie intersection lies directly on an edge
				// NOTE: to be exact, we should normalize the push instead of multiplying with
				// 0.0001 but in most cases this should be fine
				if( aAlpha <= 0.00001f ) {
					// aPrev is degenerate
					TMPO_VECTOR* v = &a->data[aPrevIndex].pos;
					v->x -= bDir.y * 0.0001f;
					v->y += bDir.x * 0.0001f;
					continue;
				}
				if( aAlpha >= 0.99999f ) {
					// aCurrent is degenerate
					TMPO_VECTOR* v = &aVertex->pos;
					v->x -= bDir.y * 0.0001f;
					v->y += bDir.x * 0.0001f;
					continue;
				}
				if( bAlpha <= 0.00001f ) {
					// bPrev is degenerate
					TMPO_VECTOR* v = &b->data[bPrevIndex].pos;
					v->x -= aDir.y * 0.0001f;
					v->y += aDir.x * 0.0001f;
					continue;
				}
				if( bAlpha >= 0.99999f ) {
					// bCurrent is degenerate
					TMPO_VECTOR* v = &bVertex->pos;
					v->x -= aDir.y * 0.0001f;
					v->y += aDir.x * 0.0001f;
					continue;
				}

				TMPO_VECTOR intersection = {aPrev.x + aAlpha * aDir.x, aPrev.y + aAlpha * aDir.y};
				tmpo_size_t aIndex    = clipPolyFindIntersectionIndex( a, aVertex->prev, aAlpha );
				tmpo_size_t bIndex    = clipPolyFindIntersectionIndex( b, bVertex->prev, bAlpha );
				tmpo_size_t aNeighbor = b->size;
				tmpo_size_t bNeighbor = a->size;
				clipPolyCreateIntersection( a, aIndex, intersection, aNeighbor, aAlpha );
				clipPolyCreateIntersection( b, bIndex, intersection, bNeighbor, bAlpha );
			}
			bPrevIndex = j;
			++j;
		}
	}
}

static int clipPolyPointInsidePoly( ClipVertices* poly, TMPO_VECTOR_CONST_REF p )
{
	unsigned cn           = 0;
	tmpo_size_t count     = poly->originalSize;
	tmpo_size_t prevIndex = count - 1;
	for( tmpo_size_t i = 0; i < count; prevIndex = i, ++i ) {
		TMPO_VECTOR* cur  = &poly->data[i].pos;
		TMPO_VECTOR* prev = &poly->data[prevIndex].pos;

		// simple bounds check whether the lines can even intersect
		if( ( p.y <= prev->y && p.y > cur->y ) || ( p.y > prev->y && p.y <= cur->y ) ) {
			// calculate intersection with horizontal ray
			float alpha         = ( prev->y - p.y ) / ( prev->y - cur->y );
			float xIntersection = prev->x + alpha * ( cur->x - prev->x );
			if( p.x < xIntersection ) {
				++cn;
			}
		}
	}

	// if the counter is even, the point is outside, otherwise the point is inside
	return ( cn % 2 );
}
static void clipPolyMarkEntryExitPointsSingle( ClipVertices* current, ClipVertices* other,
                                               ClipFollowDirection dir )
{
	tmpo_size_t count = current->size;
	if( count > 0 ) {
		int inside = clipPolyPointInsidePoly( other, current->data[0].pos );
		if( dir != CFD_FORWARD ) {
			inside = !inside;
		}
		tmpo_size_t i = current->data[0].next;
		do {
			ClipVertex* entry = &current->data[i];
			if( entry->flags & CVF_INTERSECT ) {
				if( inside ) {
					entry->flags |= CVF_EXIT;
				}
				inside = !inside;
			}
			i = entry->next;
		} while( i != 0 );
	}
}

TMPO_DEF void clipPolyMarkEntryExitPoints( ClipVertices* a, ClipVertices* b,
                                           ClipFollowDirection aDir, ClipFollowDirection bDir )
{
	TMPO_ASSERT( a );
	TMPO_ASSERT( b );

	clipPolyMarkEntryExitPointsSingle( a, b, aDir );
	clipPolyMarkEntryExitPointsSingle( b, a, bDir );
}

TMPO_DEF ClipPolyResult clipPolyEmitClippedPolygons( ClipVertices* a, ClipVertices* b,
                                                     ClipPolygonEntry* polygons,
                                                     tmpo_size_t maxPolygons, TMPO_VECTOR* vertices,
                                                     tmpo_size_t maxCount )
{
	TMPO_ASSERT( a );
	TMPO_ASSERT( b );
	TMPO_ASSERT( vertices );

	ClipPolyResult result = {0};

	if( a->size < 1 ) {
		return result;
	}

	ClipVertices* currentVertices = a;
	ClipVertices* otherVertices   = b;
	// first vertex is never an intersection
	tmpo_size_t i = currentVertices->data[0].next;

	tmpo_size_t currentPoly = 1;
	tmpo_size_t polyCount   = 0;

	tmpo_size_t put      = 0;
	tmpo_size_t size     = maxCount;
	ClipVertex* current  = &currentVertices->data[i];
	int hasIntersections = 0;
	while( i != 0 ) {
		// current is intersection but not processed
		if( ( current->flags & ( CVF_INTERSECT | CVF_PROCESSED ) ) == CVF_INTERSECT ) {
			current->flags |= CVF_PROCESSED;
			hasIntersections = 1;

			// newPolygon
			if( currentPoly < polyCount ) {
				polygons[currentPoly].size =
				    ( tmpo_size_t )( &vertices[put] - polygons[currentPoly].vertices );
			}
			if( polyCount + 1 > maxPolygons ) {
				TMPO_ASSERT( 0 && "out of memory" );
				result.polygons = (tmpo_size_t)polyCount;
				result.vertices = (tmpo_index)put;
				return result;
			}
			currentPoly                    = polyCount++;
			polygons[currentPoly].vertices = &vertices[put];
			tmpo_size_t start              = i;
			ClipVertices* startVertices    = currentVertices;
			do {
				if( current->flags & CVF_EXIT ) {
					do {
						i       = current->prev;
						current = &currentVertices->data[i];
						current->flags |= CVF_PROCESSED;
						if( put + 1 > size ) {
							TMPO_ASSERT( 0 && "out of memory" );
							result.polygons = (tmpo_size_t)polyCount;
							result.vertices = (tmpo_index)put;
							return result;
						}
						vertices[put++] = current->pos;
					} while( !( current->flags & CVF_INTERSECT ) );
				} else {
					do {
						i       = current->next;
						current = &currentVertices->data[i];
						current->flags |= CVF_PROCESSED;
						if( put + 1 > size ) {
							TMPO_ASSERT( 0 && "out of memory" );
							result.polygons = (tmpo_size_t)polyCount;
							result.vertices = (tmpo_index)put;
							return result;
						}
						vertices[put++] = current->pos;
					} while( !( current->flags & CVF_INTERSECT ) );
				}
				// ClipVertex* prev = current;
				i                  = current->neighbor;
				ClipVertices* temp = currentVertices;
				currentVertices    = otherVertices;
				otherVertices      = temp;
				current            = &currentVertices->data[i];
				current->flags |= CVF_PROCESSED;
				TMPO_ASSERT( current->flags & CVF_INTERSECT );
				// TMPO_ASSERT( ( current->flags & CVF_EXIT ) == ( prev->flags & CVF_EXIT ) );
			} while( i != start || currentVertices != startVertices );
		}
		i       = current->next;
		current = &currentVertices->data[i];
	}

	if( !hasIntersections ) {
		// is a completely inside b?
		if( clipPolyPointInsidePoly( b, a->data[0].pos ) ) {
			if( polyCount + 1 > maxPolygons ) {
				TMPO_ASSERT( 0 && "out of memory" );
				result.polygons = (tmpo_size_t)polyCount;
				result.vertices = (tmpo_index)put;
				return result;
			}
			currentPoly                    = polyCount++;
			polygons[currentPoly].vertices = &vertices[put];
			if( size > a->originalSize ) {
				size = a->originalSize;
			}
			for( tmpo_size_t j = 0; j < size; ++j ) {
				vertices[j] = a->data[j].pos;
			}
			put = size;
		} else if( b->size && clipPolyPointInsidePoly( a, b->data[0].pos ) ) {
			if( polyCount + 1 > maxPolygons ) {
				TMPO_ASSERT( 0 && "out of memory" );
				result.polygons = (tmpo_size_t)polyCount;
				result.vertices = (tmpo_index)put;
				return result;
			}
			currentPoly                    = polyCount++;
			polygons[currentPoly].vertices = &vertices[put];
			if( size > b->originalSize ) {
				size = b->originalSize;
			}
			for( tmpo_size_t j = 0; j < size; ++j ) {
				vertices[j] = b->data[j].pos;
			}
			put = size;
		}
	}
	if( currentPoly < polyCount ) {
		polygons[currentPoly].size =
		    ( tmpo_size_t )( &vertices[put] - polygons[currentPoly].vertices );
	}
	result.polygons = (tmpo_size_t)polyCount;
	result.vertices = (tmpo_index)put;
	return result;
}

TMPO_DEF tmpo_size_t clipPolyEmitClippedPolygon( ClipVertices* a, ClipVertices* b,
                                                 TMPO_VECTOR* vertices, tmpo_size_t maxCount )
{
	ClipPolygonEntry entry = {0};
	clipPolyEmitClippedPolygons( a, b, &entry, 1, vertices, maxCount );
	return entry.size;
}

#ifdef __cplusplus
}
#endif

#endif  // TM_POLYGON_IMPLEMENTATION
