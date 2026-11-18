// Pure helpers for the bookmark tree UI. Kept free of Svelte so the filter
// and folder-listing rules can be unit-tested.

export interface BookmarkNode {
  id: number;
  type: 'folder' | 'url';
  title: string;
  url?: string;
  children?: BookmarkNode[];
}

export interface FolderOption {
  id: number;
  title: string;
  depth: number;
}

/**
 * Filters the tree by a query, keeping folders that contain a match (or whose
 * own title matches). An empty query returns the tree unchanged. Matching is
 * case-insensitive over title and URL.
 */
export function filterTree(nodes: BookmarkNode[], query: string): BookmarkNode[] {
  const q = query.trim().toLowerCase();
  if (!q) return nodes;

  const matches = (node: BookmarkNode): boolean =>
    node.title.toLowerCase().includes(q) || (node.url ?? '').toLowerCase().includes(q);

  const walk = (list: BookmarkNode[]): BookmarkNode[] => {
    const out: BookmarkNode[] = [];
    for (const node of list) {
      if (node.type === 'url') {
        if (matches(node)) out.push(node);
        continue;
      }
      const kids = walk(node.children ?? []);
      if (kids.length > 0 || matches(node)) {
        out.push({ ...node, children: kids });
      }
    }
    return out;
  };

  return walk(nodes);
}

/**
 * Lists every folder as a move destination, depth-first, with an indentation
 * depth. `excludeId` (and its subtree) is skipped so a folder cannot be moved
 * into itself or a descendant.
 */
export function folderOptions(nodes: BookmarkNode[], excludeId?: number): FolderOption[] {
  const out: FolderOption[] = [];
  const walk = (list: BookmarkNode[], depth: number): void => {
    for (const node of list) {
      if (node.type !== 'folder') continue;
      if (excludeId !== undefined && node.id === excludeId) continue;
      out.push({ id: node.id, title: node.title, depth });
      walk(node.children ?? [], depth + 1);
    }
  };
  walk(nodes, 0);
  return out;
}

/** Counts URL bookmarks in a subtree. */
export function countBookmarks(nodes: BookmarkNode[]): number {
  return nodes.reduce(
    (sum, node) => sum + (node.type === 'url' ? 1 : countBookmarks(node.children ?? [])),
    0,
  );
}