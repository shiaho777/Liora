import { describe, expect, it } from 'vitest';
import { countBookmarks, filterTree, folderOptions, type BookmarkNode } from './bookmark-tree';

const tree: BookmarkNode[] = [
  {
    id: 1,
    type: 'folder',
    title: 'Bookmarks bar',
    children: [
      { id: 10, type: 'url', title: 'Svelte', url: 'https://svelte.dev' },
      {
        id: 11,
        type: 'folder',
        title: 'Work',
        children: [
          { id: 12, type: 'url', title: 'Docs', url: 'https://docs.example.com' },
          { id: 13, type: 'url', title: 'Mail', url: 'https://mail.example.com' },
        ],
      },
    ],
  },
  {
    id: 2,
    type: 'folder',
    title: 'Other bookmarks',
    children: [{ id: 20, type: 'url', title: 'News', url: 'https://news.example.com' }],
  },
];

describe('filterTree', () => {
  it('returns the tree unchanged for an empty query', () => {
    expect(filterTree(tree, '  ')).toBe(tree);
  });

  it('keeps only matching URLs and their ancestor folders', () => {
    const result = filterTree(tree, 'docs');
    expect(result).toHaveLength(1);
    expect(result[0].id).toBe(1);
    const work = result[0].children?.find((node) => node.id === 11);
    expect(work?.children).toHaveLength(1);
    expect(work?.children?.[0].id).toBe(12);
  });

  it('matches case-insensitively on title and URL', () => {
    expect(filterTree(tree, 'SVELTE')).toHaveLength(1);
    expect(filterTree(tree, 'mail.example')[0].children?.[0].children).toHaveLength(1);
  });

  it('keeps a folder whose own title matches even with no matching children', () => {
    const result = filterTree(tree, 'work');
    const work = result[0].children?.find((node) => node.id === 11);
    expect(work).toBeDefined();
    expect(work?.children).toHaveLength(0);
  });

  it('drops branches with no match', () => {
    const result = filterTree(tree, 'svelte');
    expect(result).toHaveLength(1);
    expect(result[0].children).toHaveLength(1);
    expect(result[0].children?.[0].id).toBe(10);
  });

  it('returns nothing when nothing matches', () => {
    expect(filterTree(tree, 'zzzz-no-match')).toHaveLength(0);
  });
});

describe('folderOptions', () => {
  it('lists folders depth-first with indentation depth', () => {
    const options = folderOptions(tree);
    expect(options.map((option) => option.id)).toEqual([1, 11, 2]);
    expect(options.map((option) => option.depth)).toEqual([0, 1, 0]);
  });

  it('excludes a folder and its subtree from move destinations', () => {
    const options = folderOptions(tree, 1);
    expect(options.map((option) => option.id)).toEqual([2]);
  });

  it('excludes a nested folder without removing its siblings', () => {
    const options = folderOptions(tree, 11);
    expect(options.map((option) => option.id)).toEqual([1, 2]);
  });
});

describe('countBookmarks', () => {
  it('counts only URL nodes across the whole tree', () => {
    expect(countBookmarks(tree)).toBe(4);
  });

  it('returns zero for an empty tree', () => {
    expect(countBookmarks([])).toBe(0);
  });
});