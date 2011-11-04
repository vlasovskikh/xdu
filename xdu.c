/*
 *			X D U . C
 *
 * Display the output of "du" in an X window.
 *
 * Phillip C. Dykstra
 * <phil@arl.mil>
 * 4 Sep 1991.
 * 
 * Copyright (c)	Phillip C. Dykstra	1991, 1993, 1994
 * The X Consortium, and any party obtaining a copy of these files from
 * the X Consortium, directly or indirectly, is granted, free of charge, a
 * full and unrestricted irrevocable, world-wide, paid up, royalty-free,
 * nonexclusive right and license to deal in this software and
 * documentation files (the "Software"), including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons who receive
 * copies from any such party to do so.  This license includes without
 * limitation a license to do the foregoing actions under any patents of
 * the party supplying this software to the X Consortium.
 */
#include <stdio.h>
#include "version.h"

extern char *malloc(), *calloc();

#define	MAXDEPTH	80	/* max elements in a path */
#define	MAXNAME		1024	/* max pathname element length */
#define	MAXPATH		4096	/* max total pathname length */
#define	NCOLS		5	/* default number of columns in display */

/* What we IMPORT from xwin.c */
extern int xsetup(), xmainloop(), xdrawrect(), xrepaint();

/* What we EXPORT to xwin.c */
extern int press(), reset(), repaint(), setorder(), reorder();
extern nodeinfo(), helpinfo();
int ncols = NCOLS;

/* internal routines */
char *strdup();
void addtree();
void parse_file();
void parse_entry();
void dumptree();
void clearrects();
void sorttree();

/* order to sort paths by */
#define	ORD_FIRST	1
#define	ORD_LAST	2
#define	ORD_ALPHA	3
#define	ORD_SIZE	4
#define	ORD_RALPHA	5
#define	ORD_RSIZE	6
#define	ORD_DEFAULT	ORD_FIRST
int order = ORD_DEFAULT;

/*
 * Rectangle Structure
 * Stores window coordinates of a displayed rectangle
 * so that we can "find" it again on key presses.
 */
struct rect {
	int	left;
	int	top;
	int	width;
	int	height;
};

/*
 * Node Structure
 * Each node in the path tree is linked in with one of these.
 */
struct node {
	char	*name;
	long	size;		/* from here down in the tree */
	long	num;		/* entry number - for resorting */
	struct	rect rect;	/* last drawn screen rectangle */
	struct	node *peer;	/* siblings */
	struct	node *child;	/* list of children if !NULL */
	struct	node *parent;	/* backpointer to parent */
} top;
struct node *topp = &top;
#define	NODE_NULL ((struct node *)0)
long nnodes = 0;

/*
 * create a new node with the given name and size info
 */
struct node *
makenode(name,size)
char *name;
int size;
{
	struct	node	*np;

	np = (struct node *)calloc(1,sizeof(struct node));
	np->name = strdup(name);
	np->size = size;
	np->num = nnodes;
	nnodes++;

	return	np;
}

/*
 * Return the node (if any) which has a draw rectangle containing
 * the given x,y point.
 */
struct node *
findnode(treep, x, y)
struct	node *treep;
int	x, y;
{
	struct	node	*np;
	struct	node	*np2;

	if (treep == NODE_NULL)
		return	NODE_NULL;

	if (x >= treep->rect.left && x < treep->rect.left+treep->rect.width
	 && y >= treep->rect.top && y < treep->rect.top+treep->rect.height) {
		/*printf("found %s\n", treep->name);*/
		return	treep;	/* found */
	}

	/* for each child */
	for (np = treep->child; np != NULL; np = np->peer) {
		if ((np2 = findnode(np,x,y)) != NODE_NULL)
			return	np2;
	}
	return	NODE_NULL;
}

/*
 * return a count of the number of children of a given node
 */
int
numchildren(nodep)
struct node *nodep;
{
	int	n;

	if (nodep == NODE_NULL)
		return	0;

	n = 0;
	for (nodep = nodep->child; nodep != NODE_NULL; nodep=nodep->peer)
		n++;

	return	n;
}

/*
 * fix_tree - This function repairs the tree when certain nodes haven't
 * 	      had their sizes initialized. [DPT911113]
 *	      * * * This function is recursive * * *
 */
long
fix_tree(top)
struct node *top;
{
	struct node *nd;

	if (top == NODE_NULL)		/* recursion end conditions */
		return 0;
	if (top->size >= 0) 		/* also halt recursion on valid size */
		return top->size;	/* (remember: sizes init. to -1) */

	top->size = 0;
	for (nd = top->child; nd != NODE_NULL; nd = nd->peer)
		top->size += fix_tree(nd);

	return top->size;
}

static char usage[] = "\
Usage: xdu [-options ...] filename\n\
   or  xdu [-options ...] < du.out\n\
\n\
Graphically displays the output of du in an X window\n\
  options include:\n\
  -s          Don't display size information\n\
  +s          Display size information (default)\n\
  -n          Sort in numerical order (largest first)\n\
  -rn         Sort in reverse numerical order\n\
  -a          Sort in alphabetical order\n\
  -ra         Sort in reverse alphabetical order\n\
  -c num      Set number of columns to num\n\
  Toolkit options: -fg, -bg, -rv, -display, -geometry, etc.\n\
";

main(argc,argv)
int argc;
char **argv;
{
	top.name = strdup("[root]");
	top.size = -1;

	xsetup(&argc,argv);
	if (argc == 1) {
		if (isatty(fileno(stdin))) {
			fprintf(stderr, usage);
			exit(1);
		} else {
			parse_file("-");
		}
	} else if (argc == 2 && strcmp(argv[1],"-help") != 0) {
		parse_file(argv[1]);
	} else {
		fprintf(stderr, usage);
		exit(1);
	}
	top.size = fix_tree(&top);

	/*dumptree(&top,0);*/
	if (order != ORD_DEFAULT)
		sorttree(&top, order);

	topp = &top;
	/* don't display root if only one child */
	if (numchildren(topp) == 1)
		topp = topp->child;

	xmainloop();
	exit(0);
}

void
parse_file(filename)
char *filename;
{
	char	buf[4096];
	char	name[4096];
	int	size;
	FILE	*fp;

	if (strcmp(filename, "-") == 0) {
		fp = stdin;
	} else {
		if ((fp = fopen(filename, "r")) == 0) {
			fprintf(stderr, "xdu: can't open \"%s\"\n", filename);
			exit(1);
		}
	}
	while (fgets(buf,sizeof(buf),fp) != NULL) {
		sscanf(buf, "%d %s\n", &size, name);
		/*printf("%d %s\n", size, name);*/
		parse_entry(name,size);
	}
	fclose(fp);
}

/* bust up a path string and link it into the tree */
void
parse_entry(name,size)
char *name;
int size;
{
	char	*path[MAXDEPTH]; /* break up path into this list */
	char	buf[MAXNAME];	 /* temp space for path element name */
	int	arg, indx;
	int	length;		/* nelson@reed.edu - trailing / fix */

	if (*name == '/')
		name++;		/* skip leading / */

	length = strlen(name);
	if ((length > 0) && (name[length-1] == '/')) {
		/* strip off trailing / (e.g. GNU du) */
		name[length-1] = 0;
	}

	arg = 0; indx = 0;
	bzero(path,sizeof(path));
	bzero(buf,sizeof(buf));
	while (*name != NULL) {
		if (*name == '/') {
			buf[indx] = 0;
			path[arg++] = strdup(buf);
			indx = 0;
			if (arg >= MAXDEPTH)
				break;
		} else {
			buf[indx++] = *name;
			if (indx >= MAXNAME)
				break;
		}
		name++;
	}
	buf[indx] = 0;
	path[arg++] = strdup(buf);
	path[arg] = NULL;

	addtree(&top,path,size);
}

/*
 *  Determine where n1 should go compared to n2
 *    based on the current sorting order.
 *  Return -1 if is should be before.
 *          0 if it is a toss up.
 *	    1 if it should go after.
 */
int
compare(n1,n2,order)
struct node *n1, *n2;
int order;
{
	int	ret;

	switch (order) {
	case ORD_SIZE:
		ret = n2->size - n1->size;
		if (ret == 0)
			return strcmp(n1->name,n2->name);
		else
			return ret;
		break;
	case ORD_RSIZE:
		ret = n1->size - n2->size;
		if (ret == 0)
			return strcmp(n1->name,n2->name);
		else
			return ret;
		break;
	case ORD_ALPHA:
		return strcmp(n1->name,n2->name);
		break;
	case ORD_RALPHA:
		return strcmp(n2->name,n1->name);
		break;
	case ORD_FIRST:
		/*return -1;*/
		return (n1->num - n2->num);
		break;
	case ORD_LAST:
		/*return 1;*/
		return (n2->num - n1->num);
		break;
	}

	/* shouldn't get here */
	fprintf(stderr,"xdu: bad insertion order\n");
	return	0;
}

void
insertchild(nodep,childp,order)
struct node *nodep;	/* parent */
struct node *childp;	/* child to be added */
int order;		/* FIRST, LAST, ALPHA, SIZE */
{
	struct node *np, *np1;

	if (nodep == NODE_NULL || childp == NODE_NULL)
		return;
	if (childp->peer != NODE_NULL) {
		fprintf(stderr, "xdu: can't insert child with peers\n");
		return;
	}

	childp->parent = nodep;
	if (nodep->child == NODE_NULL) {
		/* no children, order doesn't matter */
		nodep->child = childp;
		return;
	}
	/* nodep has at least one child already */
	if (compare(childp,nodep->child,order) < 0) {
		/* new first child */
		childp->peer = nodep->child;
		nodep->child = childp;
		return;
	}
	np1 = nodep->child;
	for (np = np1->peer; np != NODE_NULL; np = np->peer) {
		if (compare(childp,np,order) < 0) {
			/* insert between np1 and np */
			childp->peer = np;
			np1->peer = childp;
			return;
		}
		np1 = np;
	}
	/* at end, link new child on */
	np1->peer = childp;
}

/* add path as a child of top - recursively */
void
addtree(top, path, size)
struct node *top;
char *path[];
int size;
{
	struct	node *np;

	/*printf("addtree(\"%s\",\"%s\",%d)\n", top->name, path[0], size);*/

	/* check all children for a match */
	for (np = top->child; np != NULL; np = np->peer) {
		if (strcmp(path[0],np->name) == 0) {
			/* name matches */
			if (path[1] == NULL) {
				/* end of the chain, save size */
				np->size = size;
				return;
			}
			/* recurse */
			addtree(np,&path[1],size);
			return;
		}
	}
	/* no child matched, add a new child */
	np = makenode(path[0],-1);
	insertchild(top,np,order);

	if (path[1] == NULL) {
		/* end of the chain, save size */
		np->size = size;
		return;
	}
	/* recurse */
	addtree(np,&path[1],size);
	return;
}

/* debug tree print */
void
dumptree(np,level)
struct node *np;
int level;
{
	int	i;
	struct	node *subnp;

	for (i = 0; i < level; i++)
		printf("   ");

	printf("%s %d\n", np->name, np->size);
	for (subnp = np->child; subnp != NULL; subnp = subnp->peer) {
		dumptree(subnp,level+1);
	}
}

void
sorttree(np, order)
struct node *np;
int order;
{
	struct	node *subnp;
	struct	node *np0, *np1, *np2, *np3;

	/* sort the trees of each of this nodes children */
	for (subnp = np->child; subnp != NODE_NULL; subnp = subnp->peer) {
		sorttree(subnp, order);
	}
	/* then put the given nodes children in order */
	np0 = np;	/* np0 points to node before np1 */
	for (np1 = np->child; np1 != NODE_NULL; np1 = np1->peer) {
		np2 = np1;	/* np2 points to node before np3 */
		for (np3 = np1->peer; np3 != NODE_NULL; np3 = np3->peer) {
			if (compare(np3,np1,order) < 0) {
				/* swap links */
				if (np0 == np)
					np0->child = np3;
				else
					np0->peer = np3;
				np2->peer = np3->peer;
				np3->peer = np1;

				/* adjust pointers */
				np1 = np3;
				np3 = np2;
			}
			np2 = np3;
		}
		np0 = np1;
	}
}

/*
 * Draws a node in the given rectangle, and all of its children
 * to the "right" of the given rectangle.
 */
drawnode(nodep, rect)
struct node *nodep;	/* node whose children we should draw */
struct rect rect;	/* rectangle to draw all children in */
{
	struct rect subrect;

	/*printf("Drawing \"%s\" %d\n", nodep->name, nodep->size);*/

	xdrawrect(nodep->name, nodep->size,
		rect.left,rect.top,rect.width,rect.height);

	/* save current screen rectangle for lookups */
	nodep->rect.left = rect.left;
	nodep->rect.top = rect.top;
	nodep->rect.width = rect.width;
	nodep->rect.height = rect.height;

	/* draw children in subrectangle */
	subrect.left = rect.left+rect.width;
	subrect.top = rect.top;
	subrect.width = rect.width;
	subrect.height = rect.height;
	drawchildren(nodep, subrect);
}

/*
 * Draws all children of a node within the given rectangle.
 * Recurses on children.
 */
drawchildren(nodep, rect)
struct node *nodep;	/* node whose children we should draw */
struct rect rect;	/* rectangle to draw all children in */
{
	int	totalsize;
	int	totalheight;
	struct	node	*np;
	double	fractsize;
	int	height;
	int	top;

	/*printf("Drawing children of \"%s\", %d\n", nodep->name, nodep->size);*/
	/*printf("In [%d,%d,%d,%d]\n", rect.left,rect.top,rect.width,rect.height);*/

	top = rect.top;
	totalheight = rect.height;
	totalsize = nodep->size;
	if (totalsize == 0) {
		/* total the sizes of the children */
		totalsize = 0;
		for (np = nodep->child; np != NULL; np = np->peer)
			totalsize += np->size;
		nodep->size = totalsize;
	}

	/* for each child */
	for (np = nodep->child; np != NULL; np = np->peer) {
		fractsize = np->size / (double)totalsize;
		height = fractsize * totalheight + 0.5;
		if (height > 1) {
			struct rect subrect;
			/*printf("%s, drawrect[%d,%d,%d,%d]\n", np->name,
				rect.left,top,rect.width,height);*/
			xdrawrect(np->name, np->size,
				rect.left,top,rect.width,height);

			/* save current screen rectangle for lookups */
			np->rect.left = rect.left;
			np->rect.top = top;
			np->rect.width = rect.width;
			np->rect.height = height;

			/* draw children in subrectangle */
			subrect.left = rect.left+rect.width;
			subrect.top = top;
			subrect.width = rect.width;
			subrect.height = height;
			drawchildren(np, subrect);

			top += height;
		}
	}
}

/*
 * clear the rectangle information of a given node
 * and all of its decendents
 */
void
clearrects(nodep)
struct	node *nodep;
{
	struct	node	*np;

	if (nodep == NODE_NULL)
		return;

	nodep->rect.left = 0;
	nodep->rect.top = 0;
	nodep->rect.width = 0;
	nodep->rect.height = 0;

	/* for each child */
	for (np = nodep->child; np != NULL; np = np->peer) {
		clearrects(np);
	}
}

pwd()
{
	struct node *np;
	struct node *stack[MAXDEPTH];
	int num = 0;
	struct node *rootp;
	char path[MAXPATH];

	rootp = &top;
	if (numchildren(rootp) == 1)
		rootp = rootp->child;

	np = topp;
	while (np != NODE_NULL) {
		stack[num++] = np;
		if (np == rootp)
			break;
		np = np->parent;
	}

	path[0] = '\0';
	while (--num >= 0) {
		strcat(path,stack[num]->name);
		if (num != 0)
			strcat(path,"/");
	}
	printf("%s %d (%.2f%%)\n", path, topp->size,
		100.0*topp->size/rootp->size);
}

char *
strdup(s)
char *s;
{
	int	n;
	char	*cp;

	n = strlen(s);
	cp = malloc(n+1);
	strcpy(cp,s);

	return	cp;
}

/**************** External Entry Points ****************/

int
press(x,y)
int x, y;
{
	struct node *np;

	/*printf("press(%d,%d)...\n",x,y);*/
	np = findnode(&top,x,y);
	/*printf("Found \"%s\"\n", np?np->name:"(null)");*/
	if (np == topp) {
		/* already top, go up if possible */
		if (np->parent != &top || numchildren(&top) != 1)
			np = np->parent;
		/*printf("Already top, parent = \"%s\"\n", np?np->name:"(null)");*/
	}
	if (np != NODE_NULL) {
		topp = np;
		xrepaint();
	}
}

int
reset()
{
	topp = &top;
	if (numchildren(topp) == 1)
		topp = topp->child;
	xrepaint();
}

int
repaint(width,height)
int width, height;
{
	struct	rect rect;

	/* define a rectangle to draw into */
	rect.top = 0;
	rect.left = 0;
	rect.width = width/ncols;
	rect.height = height;

	clearrects(&top);	/* clear current rectangle info */
	drawnode(topp,rect);	/* draw tree into given rectangle */
#if 0
	pwd();			/* display current path */
#endif
}

int
setorder(op)
char *op;
{
	if (strcmp(op, "size") == 0) {
		order = ORD_SIZE;
	} else if (strcmp(op, "rsize") == 0) {
		order = ORD_RSIZE;
	} else if (strcmp(op, "alpha") == 0) {
		order = ORD_ALPHA;
	} else if (strcmp(op, "ralpha") == 0) {
		order = ORD_RALPHA;
	} else if (strcmp(op, "first") == 0) {
		order = ORD_FIRST;
	} else if (strcmp(op, "last") == 0) {
		order = ORD_LAST;
	} else if (strcmp(op, "reverse") == 0) {
		switch (order) {
		case ORD_ALPHA:
			order = ORD_RALPHA;
			break;
		case ORD_RALPHA:
			order = ORD_ALPHA;
			break;
		case ORD_SIZE:
			order = ORD_RSIZE;
			break;
		case ORD_RSIZE:
			order = ORD_SIZE;
			break;
		case ORD_FIRST:
			order = ORD_LAST;
			break;
		case ORD_LAST:
			order = ORD_FIRST;
			break;
		}
	} else {
		fprintf(stderr, "xdu: bad order \"%s\"\n", op);
	}
}

int
reorder(op)
char *op;	/* order name */
{
	setorder(op);
	sorttree(topp, order);
	xrepaint();
}

int
nodeinfo()
{
	struct node *np;

	/* display current root path */
	pwd();

	/* display each child of this node */
	for (np = topp->child; np != NULL; np = np->peer) {
		printf("%-8d %s\n", np->size, np->name);
	}
}

int
helpinfo()
{
	fprintf(stdout, "\n\
XDU Version %s - Keyboard Commands\n\
  a  sort alphabetically\n\
  n  sort numerically (largest first)\n\
  f  sort first-in-first-out\n\
  l  sort last-in-first-out\n\
  r  reverse sort\n\
  /  goto the root\n\
  q  quit (also Escape)\n\
  i  info to standard out\n\
0-9  set number of columns (0=10)\n\
", XDU_VERSION);
}
