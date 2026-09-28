# name: Analogue
# summary: A real clock face with sweeping hands, the weekday and the date beside it.
# author: Stipple
# tags: clock, time, analogue
# panel: 52x16

import string

# A dial on a panel sixteen pixels tall.
#
# The whole design follows from one number: the face can be fifteen pixels
# across and no more, so the hour hand is four pixels long and the minute
# hand six. At that size the *only* thing that distinguishes them is length,
# which is why they are also drawn in different colours and why the hour hand
# is drawn last - overlapping hands at 12:00 have to still read as two hands.
#
# A word clock was the first idea and does not fit: the 5x7 font advances six
# pixels, so a line holds eight characters, and "QUARTER PAST" is twelve.
#
# The hands move on the minute rather than sweeping, because a hand that
# advances by a fifteenth of a pixel is a hand that never appears to move.
# The second dot is the thing that ticks.

class App
  var SIN            # sine * 1000 for 60 positions, one per minute
  var CX, CY, R

  def init()
    self.CX = 25
    self.CY = 8
    self.R = 7

    # Sixty entries so a minute, an hour and a second all index the same
    # table - the hour hand is just minutes/12 and the second hand is
    # seconds. Computing these with math.sin would be sixty calls a frame
    # for numbers that never change.
    self.SIN = [
         0,  105,  208,  309,  407,  500,  588,  669,  743,  809,
       866,  914,  951,  978,  995, 1000,  995,  978,  951,  914,
       866,  809,  743,  669,  588,  500,  407,  309,  208,  105,
         0, -105, -208, -309, -407, -500, -588, -669, -743, -809,
      -866, -914, -951, -978, -995,-1000, -995, -978, -951, -914,
      -866, -809, -743, -669, -588, -500, -407, -309, -208, -105]
  end

  # Cosine is sine a quarter turn along. One table, two functions.
  def _cos(i)
    return self.SIN[(i + 15) % 60]
  end

  # A hand from the centre outwards. Twelve o'clock is up, which on a panel
  # means *subtracting* from y - the one place a clock and a framebuffer
  # disagree about which way is positive.
  def _hand(minute, length, colour)
    var x = self.CX + (self.SIN[minute] * length) / 1000
    var y = self.CY - (self._cos(minute) * length) / 1000
    line(self.CX, self.CY, x, y, colour)
  end

  def draw()
    clear(rgb(0, 0, 0))

    if !time_known()
      # A clock face with no time behind it is the most convincing lie this
      # panel could tell, because it looks exactly like a working clock.
      text(2, 5, "no time", rgb(180, 60, 60))
      return
    end

    # The twelve hour marks. Quarters brighter, because a dial this small is
    # read by its quarters - the other eight just stop it looking like a
    # cross.
    var t = 0
    while t < 12
      var i = t * 5
      var x = self.CX + (self.SIN[i] * self.R) / 1000
      var y = self.CY - (self._cos(i) * self.R) / 1000
      pixel(x, y, (t % 3) == 0 ? rgb(90, 100, 120) : rgb(38, 44, 55))
      t += 1
    end

    var h = hour() % 12
    var m = minute()
    var s = second()

    # The hour hand advances with the minutes: at half past three it should
    # sit between three and four, not still on three.
    self._hand((h * 5 + m / 12) % 60, 4, rgb(0, 150, 220))
    self._hand(m, 6, rgb(235, 240, 255))

    # The second as a moving dot on the rim rather than a third hand. A
    # seven-pixel hand that sweeps would spend most of its time on top of
    # the other two.
    var sx = self.CX + (self.SIN[s] * self.R) / 1000
    var sy = self.CY - (self._cos(s) * self.R) / 1000
    pixel(sx, sy, rgb(255, 130, 0))

    pixel(self.CX, self.CY, rgb(255, 255, 255))

    # The date beside the dial, in the columns it does not use. The face
    # spans 18 to 32, so the left block is 0 to 17 and the right 34 to 51 -
    # three characters each at six pixels, measured rather than guessed.
    var days = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"]
    text(0, 1, days[weekday() % 7], rgb(70, 78, 92))
    text(34, 1, string.format("%02d", day()), rgb(150, 160, 175))
    text(34, 9, string.format("%02d", month()), rgb(70, 78, 92))

    # And the hour, bottom left, for the glance that wants a number. Two
    # digits only: the dial already says which side of the hour it is.
    text(0, 9, string.format("%02d", hour()), rgb(150, 160, 175))
  end
end

return App()
