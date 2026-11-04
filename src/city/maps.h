#ifndef SC3_CITY_MAPS_H
#define SC3_CITY_MAPS_H

#include "common/api.h"
#include "common/sc3_array.h"

// Actual unsigned-byte cRZCellMap specialization owns separately allocated columns and its virtual destructor.
class CtByteCells {
public:
  CtByteCells(u32 columns, u32 rows, const u8 *fill);
  virtual ~CtByteCells();
  bool SerialRead(RsStream *record);
  bool SerialWrite(RsStream *record) const;
  u32 CellCountX() const;
  u32 CellCountY() const;
  // The original zero/one result is proven; its nominal bool/byte spelling remains unresolved.
  u8 InBounds(u32 x, u32 z) const;
  u8 InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const;
  u8 ValidCellRect(u32 x0, u32 z0, u32 x1, u32 z1) const;
  // The original callers consume the cell effects; both private nominal returns remain unresolved.
  void GetValue(u32 x, u32 z, u8 *value) const;
  void SetValue(u32 x, u32 z, const u8 *value);
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value);
  // The original callers consume only these fill effects; the private nominal return remains unresolved.
  void SetAllCells(const u8 *value);
  u32 column_count, row_count;
  u8 **columns;
};

// Actual sized byte-map base precedes crime's six additional interface subobjects. The shift selects stored cells;
// Reported dimensions remain full city dimensions. Storage retires through explicit Shutdown rather than destruction.
class CtSizedByteMap : public ScCellMap<u8> {
public:
  explicit CtSizedByteMap(i32 shift);
  virtual ~CtSizedByteMap();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  bool Init(CtCity *city, const u8 *fill) override;
  bool Shutdown() override;
  void GetValue(u32 x, u32 z, u8 *value) const override;
  void SetValue(u32 x, u32 z, const u8 *value) override;
  void SetAllCells(const u8 *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) override;

protected:
  u32 references;
  bool initialized;
  u32 width, height;
  i32 shift;
  CtByteCells *cells;
};

class CtCity;
class RsStream;
struct ScRect;

// Actual RandomIterator state is nonpolymorphic and owns only its packed-coordinate vector. Arithmetic mode uses
// the independent CRT random stream and the original signed stride table rather than a replacement random generator.
class CtRandomIterator {
public:
  CtRandomIterator();
  ~CtRandomIterator();
  // Complete traversal effects; the ignored private nominal return declaration remains unresolved.
  void Begin(u8 shuffle);
  void SetBounds(const ScRect &bounds);
  // Original two signed outputs and AL0/1; private nominal bool/BYTE spelling remains qualified.
  u8 Next(i32 *x, i32 *z);
  i8 state;
  u32 mode; // Serialized raw DWORD; the original enum declaration remains unproved.
  u8 dirty;
  i32 left, top, right, bottom, count;
  ScArray<u16> cells;
  i32 cursor, index, stride;
};

// Original LinearRandomIterator is the separate forty-byte index traversal used by sprite pooling.
// It shares the stride table but publishes exhaustion on the next unsuccessful call rather than its final output.
class CtLinearIterator {
public:
  CtLinearIterator();
  ~CtLinearIterator();
  void Begin(u8 shuffle);
  u8 Next(i32 *index);
  i8 state;
  u32 mode;
  u8 dirty;
  ScArray<u32> indices;
  i32 count, cursor, index, stride;
};

// The installed constructor calls the plain state constructor at wrapper+4 before installing its two-slot vtable.
// The exact Linux cRandomIteratorSerializable constructor has the same state and trailing vptr under its different ABI.
class CtSavedIterator : public CtRandomIterator {
public:
  CtSavedIterator();
  // Both methods retain their original city and serial-record operands; nominal private bool/BYTE stays qualified.
  // Payloads exceeding existing storage remain excluded by the native input boundary.
  virtual bool SerialRead(CtCity *city, RsStream *record);
  virtual bool SerialWrite(CtCity *city, RsStream *record) const;
};

// Actual signed-int cRZCellMap specialization owns separately allocated columns and its virtual destructor.
class CtIntCells {
public:
  CtIntCells(u32 columns, u32 rows, const i32 *fill);
  virtual ~CtIntCells();
  bool SerialRead(RsStream *record);
  bool SerialWrite(RsStream *record) const;
  // The original callers consume only these fill effects; the private nominal return remains unresolved.
  void SetAllCells(const i32 *value);
  u32 column_count, row_count;
  i32 **columns;
};

// Fire and police own this actual signed-int sized map at original complete64 and72. The shift selects stored cells;
// Reported dimensions remain full city dimensions. Storage retires through explicit Shutdown rather than destruction.
class CtSizedIntMap : public ScCellMap<i32> {
public:
  explicit CtSizedIntMap(i32 shift);
  virtual ~CtSizedIntMap();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  bool Init(CtCity *city, const i32 *fill) override;
  bool Shutdown() override;
  void GetValue(u32 x, u32 z, i32 *value) const override;
  void SetValue(u32 x, u32 z, const i32 *value) override;
  void SetAllCells(const i32 *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const i32 *value) override;

protected:
  u32 references;
  bool initialized;
  u32 width, height;
  i32 shift;
  CtIntCells *cells;
};

// Actual signed-short cRZCellMap specialization owns separately allocated columns and its virtual destructor.
class CtShortCells {
public:
  CtShortCells(u32 columns, u32 rows, const i16 *fill);
  virtual ~CtShortCells();
  bool SerialRead(RsStream *record);
  bool SerialWrite(RsStream *record) const;
  // The original callers consume only these fill effects; the private nominal return remains unresolved.
  void SetAllCells(const i16 *value);
  u32 column_count, row_count;
  i16 **columns;
};

// Crime owns this actual signed-short sized map at original complete148. The shift selects stored cells;
// Reported dimensions remain full city dimensions. Storage retires through explicit Shutdown rather than destruction.
class CtSizedShortMap : public ScCellMap<i16> {
public:
  explicit CtSizedShortMap(i32 shift);
  virtual ~CtSizedShortMap();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  bool Init(CtCity *city, const i16 *fill) override;
  bool Shutdown() override;
  void GetValue(u32 x, u32 z, i16 *value) const override;
  void SetValue(u32 x, u32 z, const i16 *value) override;
  void SetAllCells(const i16 *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const i16 *value) override;

protected:
  u32 references;
  bool initialized;
  u32 width, height;
  i32 shift;
  CtShortCells *cells;
};

// Full-resolution byte cell-map base used by the flammability receiver; its storage pointer is original word12.
class CtByteMap : public ScCellMap<u8> {
public:
  CtByteMap();
  virtual ~CtByteMap();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u32 CellCountX() const override;
  u32 CellCountZ() const override;
  void GetValueSint32(u32 x, u32 z, i32 *value) const override;
  bool InBounds(u32 x, u32 z) const override;
  bool InBounds(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool ValidRect(u32 x0, u32 z0, u32 x1, u32 z1) const override;
  bool SerialRead(RsStream *record) override;
  bool SerialWrite(RsStream *record) const override;
  bool Init(CtCity *city, const u8 *fill) override;
  bool Shutdown() override;
  void GetValue(u32 x, u32 z, u8 *value) const override;
  void SetValue(u32 x, u32 z, const u8 *value) override;
  void SetAllCells(const u8 *value) override;
  void SetValue(u32 x0, u32 z0, u32 x1, u32 z1, const u8 *value) override;

protected:
  u32 references;
  bool initialized;
  CtByteCells *cells;
};

#endif
