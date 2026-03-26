# cecup
GUI backup tool based on rsync

## TODO
- Hard links:
  add data structure to the inode map to track all names to each inode.
  then we will be able to correctly know if a hardlink is missing on the
  destination or not. Currently it uses the number of links as a heuristic,
  which will not work if some name is on another directory.
