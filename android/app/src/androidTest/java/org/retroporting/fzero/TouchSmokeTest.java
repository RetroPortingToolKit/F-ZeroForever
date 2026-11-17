package org.retroporting.fzero;

import static org.junit.Assert.*;

import android.app.Instrumentation;
import android.app.UiAutomation;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.view.*;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.*;
import java.util.*;
import org.junit.Test;

/**
 * Run with a test-owned game active. Verify input after native guest
 * filtering.
 */
public final class TouchSmokeTest {
  private Instrumentation getInstrumentation() {
    return InstrumentationRegistry.getInstrumentation();
  }
  private String newestSession() {
    File[] dirs =
        new File(getInstrumentation().getTargetContext().getFilesDir(),
                 "diagnostics")
            .listFiles(
                f -> f.isDirectory() && f.getName().startsWith("replay-"));
    if (dirs == null || dirs.length == 0)
      return "";
    Arrays.sort(dirs, Comparator.comparing(File::getName));
    return dirs[dirs.length - 1].getName();
  }
  private List<long[]> samples() throws Exception {
    File root = new File(getInstrumentation().getTargetContext().getFilesDir(),
                         "diagnostics");
    File[] sessions = root.listFiles(
        f -> f.isDirectory() && f.getName().startsWith("replay-"));
    assertNotNull(sessions);
    assertTrue(sessions.length > 0);
    Arrays.sort(sessions, Comparator.comparing(File::getName));
    File[] segments = sessions[sessions.length - 1].listFiles(
        f -> f.getName().endsWith(".inputs"));
    assertNotNull(segments);
    Arrays.sort(segments, Comparator.comparing(File::getName));
    List<long[]> rows = new ArrayList<>();
    for (File file : segments)
      try (BufferedReader in = new BufferedReader(new FileReader(file))) {
        String line;
        while ((line = in.readLine()) != null) {
          String[] columns = line.split(" ");
          if (columns.length >= 2)
            rows.add(new long[] {Long.parseLong(columns[0]),
                                 Long.parseLong(columns[1])});
        }
      }
    return rows;
  }
  private void touch(UiAutomation ui, long time, int action, float[][] points) {
    MotionEvent.PointerProperties[] props =
        new MotionEvent.PointerProperties[points.length];
    MotionEvent.PointerCoords[] coords =
        new MotionEvent.PointerCoords[points.length];
    for (int i = 0; i < points.length; i++) {
      props[i] = new MotionEvent.PointerProperties();
      props[i].id = i;
      props[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
      coords[i] = new MotionEvent.PointerCoords();
      coords[i].x = points[i][0];
      coords[i].y = points[i][1];
      coords[i].pressure = 1;
      coords[i].size = 1;
    }
    MotionEvent event = MotionEvent.obtain(
        time, SystemClock.uptimeMillis(), action, points.length, props, coords,
        0, 0, 1, 1, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0);
    assertTrue(ui.injectInputEvent(event, true));
    event.recycle();
  }
  @Test
  public void accelerateAndSteerTogether() throws Exception {
    String previous = newestSession();
    android.content.Intent game = new android.content.Intent();
    game.setClassName("org.retroporting.fzero",
                      "org.retroporting.fzero.GameActivity");
    game.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK);
    getInstrumentation().getTargetContext().startActivity(game);
    // Readiness is native frame evidence, with no elapsed-time kill cutoff.
    while (newestSession().equals(previous) || samples().size() < 3)
      SystemClock.sleep(100);
    List<long[]> before = samples();
    assertFalse(before.isEmpty());
    long start = before.get(before.size() - 1)[0];
    DisplayMetrics metrics = new DisplayMetrics();
    ((WindowManager)getInstrumentation().getTargetContext().getSystemService(
         "window"))
        .getDefaultDisplay()
        .getRealMetrics(metrics);
    float w = metrics.widthPixels, h = metrics.heightPixels,
          d = metrics.density, u = Math.min(58 * d, h / 5.2f), margin = 18 * d;
    float[] accelerate = {w - margin - u * .55f, h - margin - u * .55f};
    float[] left = {margin + u * .5f, h - margin - u * 2.6f + u * 1.3f};
    UiAutomation ui = getInstrumentation().getUiAutomation();
    long down = SystemClock.uptimeMillis();
    touch(ui, down, MotionEvent.ACTION_DOWN, new float[][] {accelerate});
    touch(ui, down,
          MotionEvent.ACTION_POINTER_DOWN |
              (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
          new float[][] {accelerate, left});
    SystemClock.sleep(700);
    touch(ui, down,
          MotionEvent.ACTION_POINTER_UP |
              (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
          new float[][] {accelerate, left});
    touch(ui, down, MotionEvent.ACTION_UP, new float[][] {accelerate});
    SystemClock.sleep(300);
    boolean combined = false, released = false;
    for (long[] row : samples())
      if (row[0] > start) {
        int pad = (int)row[1] & 4095;
        if ((pad & 65) == 65)
          combined = true;
        if (combined && pad == 0)
          released = true;
      }
    assertTrue("Native game must receive accelerate and left together",
               combined);
    assertTrue("Releasing both fingers must clear native input", released);
  }
}
