// Index to access a circular buffer.
// Currently, you can access the top of the buffer and loop over the entire buffer.
// Mainly targeted at historic data collection.
// T is a singed or unsigned counter type and C is the capacity.
template <typename T, int C>
class RoundBufferIndex
{
  public:
    T Index() { return curpos; }
    T Used() { return used; }
    operator T() { return curpos; }
    bool IsEmpty() { return used == 0; }
    T operator++()
    {
      if ( used < C ) used++;
      return Increment(curpos);
    }
    void Loop(std::function<void(T idx)> func)
    {
      T idx = used != C ? C - 1 : curpos;
      for ( T counter = used; counter > 0; counter-- )
      {
        func(Increment(idx));
      }
    }
  private:
    T Increment(T& counter)
    {
      return counter = counter == (C - 1) ? 0 : ++counter;
    }
    T curpos = C - 1;
    T used = 0;
};
