/*
 * Copyright 2013 Google Inc.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <assert.h>
#include <endian.h>
#include <stdint.h>
#include <stdio.h>

#include "base/algorithm.h"
#include "base/device_tree.h"
#include "base/xalloc.h"

/*
 * Functions for picking apart flattened trees.
 */

static uint32_t size32(uint32_t val)
{
	return (val + sizeof(uint32_t) - 1) / sizeof(uint32_t);
}

int fdt_next_property(void *blob, uint32_t offset, FdtProperty *prop)
{
	FdtHeader *header = (FdtHeader *)blob;
	uint32_t *ptr = (uint32_t *)(((uint8_t *)blob) + offset);

	int index = 0;
	if (be32toh(ptr[index++]) != TokenProperty)
		return 0;

	uint32_t size = be32toh(ptr[index++]);
	uint32_t name_offset = be32toh(ptr[index++]);
	name_offset += be32toh(header->strings_offset);

	if (prop) {
		prop->name = (char *)((uint8_t *)blob + name_offset);
		prop->data = &ptr[index];
		prop->size = size;
	}

	index += size32(size);

	return index * 4;
}

int fdt_node_name(void *blob, uint32_t offset, const char **name)
{
	uint8_t *ptr = ((uint8_t *)blob) + offset;

	if (be32toh(*(uint32_t *)ptr) != TokenBeginNode)
		return 0;

	ptr += 4;
	if (name)
		*name = (char *)ptr;
	return size32(strlen((char *)ptr) + 1) * sizeof(uint32_t) + 4;
}



/*
 * Functions for printing flattened trees.
 */

static void print_indent(int depth)
{
	while (depth--)
		printf("  ");
}

static void print_property(FdtProperty *prop, int depth)
{
	print_indent(depth);
	printf("prop \"%s\" (%d bytes).\n", prop->name, prop->size);
	print_indent(depth + 1);
	for (int i = 0; i < MIN(25, prop->size); i++) {
		printf("%02x ", ((uint8_t *)prop->data)[i]);
	}
	if (prop->size > 25)
		printf("...");
	printf("\n");
}

static int print_flat_node(void *blob, uint32_t start_offset, int depth)
{
	int offset = start_offset;
	const char *name;
	int size;

	size = fdt_node_name(blob, offset, &name);
	if (!size)
		return 0;
	offset += size;

	print_indent(depth);
	printf("name = %s\n", name);

	FdtProperty prop;
	while ((size = fdt_next_property(blob, offset, &prop))) {
		print_property(&prop, depth + 1);

		offset += size;
	}

	while ((size = print_flat_node(blob, offset, depth + 1)))
		offset += size;

	return offset - start_offset + sizeof(uint32_t);
}

void fdt_print_node(void *blob, uint32_t offset)
{
	print_flat_node(blob, offset, 0);
}



/*
 * A utility function to skip past nodes in flattened trees.
 */

int fdt_skip_node(void *blob, uint32_t start_offset)
{
	int offset = start_offset;
	int size;

	const char *name;
	size = fdt_node_name(blob, offset, &name);
	if (!size)
		return 0;
	offset += size;

	while ((size = fdt_next_property(blob, offset, NULL)))
		offset += size;

	while ((size = fdt_skip_node(blob, offset)))
		offset += size;

	return offset - start_offset + sizeof(uint32_t);
}



/*
 * Functions to turn a flattened tree into an unflattened one.
 */

static DeviceTreeNode node_cache[1000];
static int node_counter = 0;
static DeviceTreeProperty prop_cache[5000];
static int prop_counter = 0;

/*
 * Libpayload's malloc() has linear allocation complexity and goes completely
 * mental after a few thousand small requests. This little hack will absorb
 * the worst of it to avoid increasing boot time for no reason.
 */
static DeviceTreeNode *alloc_node(void)
{
	if (node_counter >= ARRAY_SIZE(node_cache))
		return xzalloc(sizeof(DeviceTreeNode));
	return &node_cache[node_counter++];
}
static DeviceTreeProperty *alloc_prop(void)
{
	if (prop_counter >= ARRAY_SIZE(prop_cache))
		return xzalloc(sizeof(DeviceTreeProperty));
	return &prop_cache[prop_counter++];
}

static int fdt_unflatten_node(void *blob, uint32_t start_offset,
			      DeviceTreeNode **new_node)
{
	ListNode *last;
	int offset = start_offset;
	const char *name;
	int size;

	size = fdt_node_name(blob, offset, &name);
	if (!size)
		return 0;
	offset += size;

	DeviceTreeNode *node = alloc_node();
	*new_node = node;
	node->name = name;

	FdtProperty fprop;
	last = &node->properties;
	while ((size = fdt_next_property(blob, offset, &fprop))) {
		DeviceTreeProperty *prop = alloc_prop();
		prop->prop = fprop;

		list_insert_after(&prop->list_node, last);
		last = &prop->list_node;

		offset += size;
	}

	DeviceTreeNode *child;
	last = &node->children;
	while ((size = fdt_unflatten_node(blob, offset, &child))) {
		list_insert_after(&child->list_node, last);
		last = &child->list_node;

		offset += size;
	}

	return offset - start_offset + sizeof(uint32_t);
}

static int fdt_unflatten_map_entry(void *blob, uint32_t offset,
				   DeviceTreeReserveMapEntry **new_entry)
{
	uint64_t *ptr = (uint64_t *)(((uint8_t *)blob) + offset);
	uint64_t start = be64toh(ptr[0]);
	uint64_t size = be64toh(ptr[1]);

	if (!size)
		return 0;

	DeviceTreeReserveMapEntry *entry = xzalloc(sizeof(*entry));
	*new_entry = entry;
	entry->start = start;
	entry->size = size;

	return sizeof(uint64_t) * 2;
}

DeviceTree *fdt_unflatten(void *blob)
{
	DeviceTree *tree = xzalloc(sizeof(*tree));
	FdtHeader *header = (FdtHeader *)blob;
	tree->header = header;

	uint32_t struct_offset = be32toh(header->structure_offset);
	uint32_t strings_offset = be32toh(header->strings_offset);
	uint32_t reserve_offset = be32toh(header->reserve_map_offset);
	uint32_t min_offset = 0;
	min_offset = MIN(struct_offset, strings_offset);
	min_offset = MIN(min_offset, reserve_offset);
	// Assume everything up to the first non-header component is part of
	// the header and needs to be preserved. This will protect us against
	// new elements being added in the future.
	tree->header_size = min_offset;

	DeviceTreeReserveMapEntry *entry;
	uint32_t offset = reserve_offset;
	int size;
	ListNode *last = &tree->reserve_map;
	while ((size = fdt_unflatten_map_entry(blob, offset, &entry))) {
		list_insert_after(&entry->list_node, last);
		last = &entry->list_node;

		offset += size;
	}

	fdt_unflatten_node(blob, struct_offset, &tree->root);

	return tree;
}



/*
 * Functions to find the size of device tree would take if it was flattened.
 */

static void dt_flat_prop_size(DeviceTreeProperty *prop, uint32_t *struct_size,
			      uint32_t *strings_size)
{
	// Starting token.
	*struct_size += sizeof(uint32_t);
	// Size.
	*struct_size += sizeof(uint32_t);
	// Name offset.
	*struct_size += sizeof(uint32_t);
	// Property value.
	*struct_size += size32(prop->prop.size) * sizeof(uint32_t);

	// Property name.
	*strings_size += strlen(prop->prop.name) + 1;
}

static void dt_flat_node_size(DeviceTreeNode *node, uint32_t *struct_size,
			      uint32_t *strings_size)
{
	// Starting token.
	*struct_size += sizeof(uint32_t);
	// Node name.
	*struct_size += size32(strlen(node->name) + 1) * sizeof(uint32_t);

	DeviceTreeProperty *prop;
	list_for_each(prop, node->properties, list_node)
		dt_flat_prop_size(prop, struct_size, strings_size);

	DeviceTreeNode *child;
	list_for_each(child, node->children, list_node)
		dt_flat_node_size(child, struct_size, strings_size);

	// End token.
	*struct_size += sizeof(uint32_t);
}

uint32_t dt_flat_size(DeviceTree *tree)
{
	uint32_t size = tree->header_size;
	DeviceTreeReserveMapEntry *entry;
	list_for_each(entry, tree->reserve_map, list_node)
		size += sizeof(uint64_t) * 2;
	size += sizeof(uint64_t) * 2;

	uint32_t struct_size = 0;
	uint32_t strings_size = 0;
	dt_flat_node_size(tree->root, &struct_size, &strings_size);

	size += struct_size;
	// End token.
	size += sizeof(uint32_t);

	size += strings_size;

	return size;
}



/*
 * Functions to flatten a device tree.
 */

static void dt_flatten_map_entry(DeviceTreeReserveMapEntry *entry,
				 void **map_start)
{
	((uint64_t *)*map_start)[0] = htobe64(entry->start);
	((uint64_t *)*map_start)[1] = htobe64(entry->size);
	*map_start = ((uint8_t *)*map_start) + sizeof(uint64_t) * 2;
}

static void dt_flatten_prop(DeviceTreeProperty *prop, void **struct_start,
			    void *strings_base, void **strings_start)
{
	uint8_t *dstruct = (uint8_t *)*struct_start;
	uint8_t *dstrings = (uint8_t *)*strings_start;

	*((uint32_t *)dstruct) = htobe32(TokenProperty);
	dstruct += sizeof(uint32_t);

	*((uint32_t *)dstruct) = htobe32(prop->prop.size);
	dstruct += sizeof(uint32_t);

	uint32_t name_offset = (uintptr_t)dstrings - (uintptr_t)strings_base;
	*((uint32_t *)dstruct) = htobe32(name_offset);
	dstruct += sizeof(uint32_t);

	strcpy((char *)dstrings, prop->prop.name);
	dstrings += strlen(prop->prop.name) + 1;

	memcpy(dstruct, prop->prop.data, prop->prop.size);
	dstruct += size32(prop->prop.size) * 4;

	*struct_start = dstruct;
	*strings_start = dstrings;
}

static void dt_flatten_node(DeviceTreeNode *node, void **struct_start,
			    void *strings_base, void **strings_start)
{
	uint8_t *dstruct = (uint8_t *)*struct_start;
	uint8_t *dstrings = (uint8_t *)*strings_start;

	*((uint32_t *)dstruct) = htobe32(TokenBeginNode);
	dstruct += sizeof(uint32_t);

	strcpy((char *)dstruct, node->name);
	dstruct += size32(strlen(node->name) + 1) * 4;

	DeviceTreeProperty *prop;
	list_for_each(prop, node->properties, list_node)
		dt_flatten_prop(prop, (void **)&dstruct, strings_base,
				(void **)&dstrings);

	DeviceTreeNode *child;
	list_for_each(child, node->children, list_node)
		dt_flatten_node(child, (void **)&dstruct, strings_base,
				(void **)&dstrings);

	*((uint32_t *)dstruct) = htobe32(TokenEndNode);
	dstruct += sizeof(uint32_t);

	*struct_start = dstruct;
	*strings_start = dstrings;
}

void dt_flatten(DeviceTree *tree, void *start_dest)
{
	uint8_t *dest = (uint8_t *)start_dest;

	memcpy(dest, tree->header, tree->header_size);
	FdtHeader *header = (FdtHeader *)dest;
	dest += tree->header_size;

	DeviceTreeReserveMapEntry *entry;
	list_for_each(entry, tree->reserve_map, list_node)
		dt_flatten_map_entry(entry, (void **)&dest);
	((uint64_t *)dest)[0] = ((uint64_t *)dest)[1] = 0;
	dest += sizeof(uint64_t) * 2;

	uint32_t struct_size = 0;
	uint32_t strings_size = 0;
	dt_flat_node_size(tree->root, &struct_size, &strings_size);

	uint8_t *struct_start = dest;
	header->structure_offset = htobe32(dest - (uint8_t *)start_dest);
	header->structure_size = htobe32(struct_size);
	dest += struct_size;

	*((uint32_t *)dest) = htobe32(TokenEnd);
	dest += sizeof(uint32_t);

	uint8_t *strings_start = dest;
	header->strings_offset = htobe32(dest - (uint8_t *)start_dest);
	header->strings_size = htobe32(strings_size);
	dest += strings_size;

	dt_flatten_node(tree->root, (void **)&struct_start, strings_start,
			(void **)&strings_start);

	header->totalsize = htobe32(dest - (uint8_t *)start_dest);
}



/*
 * Functions for printing a non-flattened device tree.
 */

static void print_node(DeviceTreeNode *node, int depth)
{
	print_indent(depth);
	printf("name = %s\n", node->name);

	DeviceTreeProperty *prop;
	list_for_each(prop, node->properties, list_node)
		print_property(&prop->prop, depth + 1);

	DeviceTreeNode *child;
	list_for_each(child, node->children, list_node)
		print_node(child, depth + 1);
}

void dt_print_node(DeviceTreeNode *node)
{
	print_node(node, 0);
}



/*
 * Functions for reading and manipulating an unflattened device tree.
 */

/*
 * Read #address-cells and #size-cells properties from a node.
 *
 * @param node		The device tree node to read from.
 * @param addrcp	Pointer to store #address-cells in, skipped if NULL.
 * @param sizecp	Pointer to store #size-cells in, skipped if NULL.
 */
void dt_read_cell_props(DeviceTreeNode *node, uint32_t *addrcp,
			uint32_t *sizecp)
{
	DeviceTreeProperty *prop;
	list_for_each(prop, node->properties, list_node) {
		if (addrcp && !strcmp("#address-cells", prop->prop.name))
			*addrcp = be32toh(*(uint32_t *)prop->prop.data);
		if (sizecp && !strcmp("#size-cells", prop->prop.name))
			*sizecp = be32toh(*(uint32_t *)prop->prop.data);
	}
}

/*
 * Find a node from a device tree path, relative to a parent node.
 *
 * @param parent	The node from which to start the relative path lookup.
 * @param path		An array of path component strings that will be looked
 * 			up in order to find the node. Must be terminated with
 * 			a NULL pointer. Example: {'firmware', 'coreboot', NULL}
 * @param addrcp	Pointer that will be updated with any #address-cells
 * 			value found in the path. May be NULL to ignore.
 * @param sizecp	Pointer that will be updated with any #size-cells
 * 			value found in the path. May be NULL to ignore.
 * @param create	1: Create node(s) if not found. 0: Return NULL instead.
 * @return		The found/created node, or NULL.
 */
DeviceTreeNode *dt_find_node(DeviceTreeNode *parent, const char **path,
			     uint32_t *addrcp, uint32_t *sizecp, int create)
{
	DeviceTreeNode *node, *found = NULL;

	// Update #address-cells and #size-cells for this level.
	dt_read_cell_props(parent, addrcp, sizecp);

	if (!*path)
		return parent;

	// Find the next node in the path, if it exists.
	list_for_each(node, parent->children, list_node) {
		if (!strcmp(node->name, *path)) {
			found = node;
			break;
		}
	}

	// Otherwise create it or return NULL.
	if (!found) {
		if (!create)
			return NULL;

		found = alloc_node();
		found->name = strdup(*path);
		if (!found->name)
			return NULL;

		list_insert_after(&found->list_node, &parent->children);
	}

	return dt_find_node(found, path + 1, addrcp, sizecp, create);
}

/*
 * Check if given node is compatible.
 *
 * @param node		The node which is to be checked for compatible property.
 * @param compat	The compatible string to match.
 * @return		1 = compatible, 0 = not compatible.
 */
static int dt_check_compat_match(DeviceTreeNode *node, const char *compat)
{
	DeviceTreeProperty *prop;

	list_for_each(prop, node->properties, list_node) {
		if (!strcmp("compatible", prop->prop.name)) {
			size_t bytes = prop->prop.size;
			const char *str = prop->prop.data;
			while (bytes > 0) {
				if (!strncmp(compat, str, bytes))
					return 1;
				size_t len = strnlen(str, bytes) + 1;
				if (bytes <= len)
					break;
				str += len;
				bytes -= len;
			}
			break;
		}
	}

	return 0;
}

/*
 * Find a node from a compatible string, in the subtree of a parent node.
 *
 * @param parent	The parent node under which to look.
 * @param compat	The compatible string to find.
 * @return		The found node, or NULL.
 */
DeviceTreeNode *dt_find_compat(DeviceTreeNode *parent, const char *compat)
{
	// Check if the parent node itself is compatible.
	if (dt_check_compat_match(parent, compat))
		return parent;

	DeviceTreeNode *child;
	list_for_each(child, parent->children, list_node) {
		DeviceTreeNode *found = dt_find_compat(child, compat);
		if (found)
			return found;
	}

	return NULL;
}

/*
 * Find the next compatible child of a given parent. All children upto the
 * child passed in by caller are ignored. If child is NULL, it considers all the
 * children to find the first child which is compatible.
 *
 * @param parent	The parent node under which to look.
 * @param child	The child node to start search from (exclusive). If NULL
 *                      consider all children.
 * @param compat	The compatible string to find.
 * @return		The found node, or NULL.
 */
DeviceTreeNode *dt_find_next_compat_child(DeviceTreeNode *parent,
					  DeviceTreeNode *child,
					  const char *compat)
{
	DeviceTreeNode *next;
	int ignore = 0;

	if (child)
		ignore = 1;

	list_for_each(next, parent->children, list_node) {
		if (ignore) {
			if (child == next)
				ignore = 0;
			continue;
		}

		if (dt_check_compat_match(next, compat))
			return next;
	}

	return NULL;
}

/*
 * Find a node with matching property value, in the subtree of a parent node.
 *
 * @param parent	The parent node under which to look.
 * @param name		The property name to look for.
 * @param data		The property value to look for.
 * @param size		The property size.
 */
DeviceTreeNode *dt_find_prop_value(DeviceTreeNode *parent, const char *name,
				   void *data, size_t size)
{
	DeviceTreeProperty *prop;

	/* Check if parent itself has the required property value. */
	list_for_each(prop, parent->properties, list_node) {
		if (!strcmp(name, prop->prop.name)) {
			size_t bytes = prop->prop.size;
			void *prop_data = prop->prop.data;
			if (size != bytes)
				break;
			if (!memcmp(data, prop_data, size))
				return parent;
			break;
		}
	}

	DeviceTreeNode *child;
	list_for_each(child, parent->children, list_node) {
		DeviceTreeNode *found = dt_find_prop_value(child, name, data,
							   size);
		if (found)
			return found;
	}
	return NULL;
}

/*
 * Write an arbitrary sized big-endian integer into a pointer.
 *
 * @param dest		Pointer to the DT property data buffer to write.
 * @param src		The integer to write (in CPU endianess).
 * @param length	the length of the destination integer in bytes.
 */
void dt_write_int(uint8_t *dest, uint64_t src, size_t length)
{
	while (length--) {
		dest[length] = (uint8_t)src;
		src >>= 8;
	}
}

/*
 * Add an arbitrary property to a node, or update it if it already exists.
 *
 * @param node		The device tree node to add to.
 * @param name		The name of the new property.
 * @param data		The raw data blob to be stored in the property.
 * @param size		The size of data in bytes.
 */
void dt_add_bin_prop(DeviceTreeNode *node, char *name, void *data, size_t size)
{
	DeviceTreeProperty *prop;

	list_for_each(prop, node->properties, list_node) {
		if (!strcmp(prop->prop.name, name)) {
			prop->prop.data = data;
			prop->prop.size = size;
			return;
		}
	}

	prop = alloc_prop();
	list_insert_after(&prop->list_node, &node->properties);
	prop->prop.name = name;
	prop->prop.data = data;
	prop->prop.size = size;
}

/*
 * Find given string property in a node and return its content.
 *
 * @param node		The device tree node to search.
 * @param name		The name of the property.
 * @return		The found string, or NULL.
 */
const char *dt_find_string_prop(DeviceTreeNode *node, const char *name)
{
	void *content;
	size_t size;

	dt_find_bin_prop(node, name, &content, &size);

	return content;
}

/*
 * Find given property in a node.
 *
 * @param node		The device tree node to search.
 * @param name		The name of the property.
 * @param data		Pointer to return raw data blob in the property.
 * @param size		Pointer to return the size of data in bytes.
 */
void dt_find_bin_prop(DeviceTreeNode *node, const char *name, void **data,
		      size_t *size)
{
	DeviceTreeProperty *prop;

	*data = NULL;
	*size = 0;

	list_for_each(prop, node->properties, list_node) {
		if (!strcmp(prop->prop.name, name)) {
			*data = prop->prop.data;
			*size = prop->prop.size;
			return;
		}
	}
}

/*
 * Add a string property to a node, or update it if it already exists.
 *
 * @param node		The device tree node to add to.
 * @param name		The name of the new property.
 * @param str		The zero-terminated string to be stored in the property.
 */
void dt_add_string_prop(DeviceTreeNode *node, char *name, char *str)
{
	dt_add_bin_prop(node, name, str, strlen(str) + 1);
}

/*
 * Add a 32-bit integer property to a node, or update it if it already exists.
 *
 * @param node		The device tree node to add to.
 * @param name		The name of the new property.
 * @param val		The integer to be stored in the property.
 */
void dt_add_u32_prop(DeviceTreeNode *node, char *name, uint32_t val)
{
	uint32_t *val_ptr = xmalloc(sizeof(val));
	*val_ptr = htobe32(val);
	dt_add_bin_prop(node, name, val_ptr, sizeof(*val_ptr));
}

/*
 * Add a 'reg' address list property to a node, or update it if it exists.
 *
 * @param node		The device tree node to add to.
 * @param addrs		Array of address values to be stored in the property.
 * @param sizes		Array of corresponding size values to 'addrs'.
 * @param count		Number of values in 'addrs' and 'sizes' (must be equal).
 * @param addr_cells	Value of #address-cells property valid for this node.
 * @param size_cells	Value of #size-cells property valid for this node.
 */
void dt_add_reg_prop(DeviceTreeNode *node, uint64_t *addrs, uint64_t *sizes,
		     int count, uint32_t addr_cells, uint32_t size_cells)
{
	int i;
	size_t length = (addr_cells + size_cells) * sizeof(uint32_t) * count;
	uint8_t *data = xmalloc(length);
	uint8_t *cur = data;

	for (i = 0; i < count; i++) {
		dt_write_int(cur, addrs[i], addr_cells * sizeof(uint32_t));
		cur += addr_cells * sizeof(uint32_t);
		dt_write_int(cur, sizes[i], size_cells * sizeof(uint32_t));
		cur += size_cells * sizeof(uint32_t);
	}

	dt_add_bin_prop(node, "reg", data, length);
}

/*
 * Fixups to apply to a kernel's device tree before booting it.
 */

ListNode device_tree_fixups;

int dt_apply_fixups(DeviceTree *tree)
{
	DeviceTreeFixup *fixup;
	list_for_each(fixup, device_tree_fixups, list_node) {
		assert(fixup->fixup);
		if (fixup->fixup(fixup, tree))
			return 1;
	}
	return 0;
}
