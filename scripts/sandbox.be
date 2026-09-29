# name: Sandbox
# summary: Falling sand. It pours, it piles, it slumps - and the button shakes the whole thing loose.
# author: Stipple
# tags: animation, physics, interactive
# panel: 52x16

# @config hue number "Colour" default=30 min=0 max=359 help="0 red, 30 sand, 120 green, 210 blue. The grains vary a little either side of it."
# @config pour boolean "Keep pouring" default=true

class App
  var W, H
  var grid
  var PAL
  var spout, drift
  var shake
  var pouring

  def init()
    self.W = width()
    self.H = height()

    self.grid = []
    var i = 0
    while i < self.W * self.H
      self.grid.push(-1)
      i += 1
    end

    # Eight shades around the chosen hue. Grains that are all one colour read
    # as a solid block the moment they settle; a little variation is what
    # makes a pile look like a pile.
    var hue = store.get("hue", 30)
    self.PAL = []
    var k = 0
    while k < 8
      self.PAL.push(self._hue(hue - 14 + k * 4, 120 + k * 16))
      k += 1
    end

    self.pouring = store.get("pour", true)
    self.spout = int(self.W / 2)
    self.drift = 1
    self.shake = 0
  end

  def duration()
    return 20000
  end

  def _hue(h, v)
    var x = h % 360
    if x < 0
      x += 360
    end
    var seg = int(x / 60)
    var f = x % 60
    var up = int(f * v / 60)
    var dn = v - up
    if seg == 0
      return rgb(v, up, 0)
    elif seg == 1
      return rgb(dn, v, 0)
    elif seg == 2
      return rgb(0, v, up)
    elif seg == 3
      return rgb(0, dn, v)
    elif seg == 4
      return rgb(up, 0, v)
    end
    return rgb(v, 0, dn)
  end

  def _get(x, y)
    if x < 0 || x >= self.W || y < 0 || y >= self.H
      return -2
    end
    return self.grid[y * self.W + x]
  end

  def _set(x, y, v)
    self.grid[y * self.W + x] = v
  end

  # A shake, not a reset. Everything above the floor gets nudged sideways and
  # loosened, so the pile slumps and reforms rather than vanishing - which is
  # the satisfying half of poking a pile of sand.
  def on_button(name)
    if name != "select"
      return
    end
    self.shake = 12
  end

  def _settle(frame)
    # Bottom row upward, or a grain would fall the whole height in one frame
    # and the sand would look like rain.
    var y = self.H - 2
    while y >= 0
      # The scan alternates direction each frame. Always sweeping left to
      # right builds a visible lean, because the leftmost grain of a row gets
      # to move into space the next one then cannot use.
      var i = 0
      while i < self.W
        var x = i
        if frame % 2 == 0
          x = self.W - 1 - i
        end
        i += 1

        var v = self._get(x, y)
        if v < 0
          continue
        end

        if self._get(x, y + 1) == -1
          self._set(x, y, -1)
          self._set(x, y + 1, v)
          continue
        end

        # Blocked underneath: it may roll off the shoulder, but not every
        # frame. Sand that always rolls has no angle of repose - it spreads
        # into a one-grain film across the floor, which is what this did
        # first. Letting a third of the grains roll per frame is enough
        # friction to build a heap with sloped sides.
        if (x * 7 + y * 3 + frame) % 3 != 0
          continue
        end

        # Preference alternates with the scan so piles stay symmetrical.
        var first = frame % 2 == 0 ? -1 : 1
        if self._get(x + first, y + 1) == -1
          self._set(x, y, -1)
          self._set(x + first, y + 1, v)
          continue
        end
        if self._get(x - first, y + 1) == -1
          self._set(x, y, -1)
          self._set(x - first, y + 1, v)
        end
      end
      y -= 1
    end
  end

  def _pour(frame)
    # The spout wanders rather than sitting still, so the pile grows into a
    # ridge instead of a single cone.
    # Slower than gravity. Moving a column per frame while a grain falls a
    # row per frame drew the stream as a diagonal line across the panel -
    # correct for a spout travelling that fast, and nothing like pouring.
    if frame % 5 == 0
      self.spout += self.drift
      if self.spout <= 1 || self.spout >= self.W - 2
        self.drift = 0 - self.drift
      end
    end
    if self._get(self.spout, 0) == -1
      self._set(self.spout, 0, self.PAL[int(frame / 3) % 8])
    end
  end

  # Once the sand reaches the top the spout is buried and nothing moves, which
  # looks broken rather than full. The floor leaks slowly instead, so the pile
  # keeps flowing and the whole thing never has to be thrown away.
  def _drain(frame)
    var filled = 0
    var x = 0
    while x < self.W
      if self._get(x, 2) >= 0
        filled += 1
      end
      x += 1
    end
    if filled < 6
      return
    end
    var hole = int(frame / 4) % self.W
    self._set(hole, self.H - 1, -1)
  end

  def draw()
    clear(rgb(0, 0, 0))
    var frame = int(now_ms() / 33)

    if self.shake > 0
      self.shake -= 1
      # Lift every grain that has a neighbour, one row, once. That is enough
      # to break the friction and let the whole pile find a new shape.
      var y = 1
      while y < self.H
        var x = 0
        while x < self.W
          var v = self._get(x, y)
          if v >= 0 && self._get(x, y - 1) == -1 && (x + y + frame) % 3 == 0
            self._set(x, y, -1)
            self._set(x, y - 1, v)
          end
          x += 1
        end
        y += 1
      end
    end

    if self.pouring
      self._pour(frame)
    end
    self._settle(frame)
    self._drain(frame)

    var y2 = 0
    while y2 < self.H
      var x2 = 0
      while x2 < self.W
        var v = self.grid[y2 * self.W + x2]
        if v >= 0
          pixel(x2, y2, v)
        end
        x2 += 1
      end
      y2 += 1
    end
  end
end

return App()
