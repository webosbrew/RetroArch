/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * Licensed under the Oculus SDK License Agreement (the "License");
 * you may not use the Oculus SDK except in compliance with the License,
 * which is provided at the time of installation or download, or which
 * otherwise accompanies this software in either electronic or hard copy form.
 *
 * You may obtain a copy of the License at
 * https://developer.oculus.com/licenses/oculussdk/
 *
 * Unless required by applicable law or agreed to in writing, the Oculus SDK
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Copyright (c) Facebook Technologies, LLC and its affiliates. All Rights reserved.
package com.oculus.sdk.xroverlaykeyboard;

import android.content.Context;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

public class MainActivity extends android.app.NativeActivity implements TextWatcher {

  private EditText mScTextView;
  private Button showKeyboardButton;
  private Button hideKeyboardButton;
  private Button clearTextButton;

  static {
    System.loadLibrary("xroverlaykeyboard");
  }

  @Override
  protected void onCreate(android.os.Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);

    // Create main layout
    LinearLayout mainLayout = new LinearLayout(this);
    mainLayout.setOrientation(LinearLayout.VERTICAL);
    mainLayout.setPadding(20, 20, 20, 20);

    // Title
    TextView titleView = new TextView(this);
    titleView.setText("Virtual Keyboard Demo");
    titleView.setTextSize(20);
    titleView.setPadding(0, 0, 0, 20);
    mainLayout.addView(titleView);

    // Description
    TextView descView = new TextView(this);
    descView.setText("This demo shows virtual keyboard functionality. In VR, these buttons would be 3D objects in space.");
    descView.setTextSize(14);
    descView.setPadding(0, 0, 0, 20);
    mainLayout.addView(descView);

    // Text input
    TextView inputLabel = new TextView(this);
    inputLabel.setText("Text Input:");
    inputLabel.setTextSize(16);
    inputLabel.setPadding(0, 0, 0, 8);
    mainLayout.addView(inputLabel);

    mScTextView = new EditText(this);
    mScTextView.setHint("Type here...");
    mScTextView.setMinLines(3);
    mScTextView.setPadding(10, 10, 10, 10);
    mainLayout.addView(mScTextView);

    // Button layout
    LinearLayout buttonLayout = new LinearLayout(this);
    buttonLayout.setOrientation(LinearLayout.HORIZONTAL);
    buttonLayout.setPadding(0, 20, 0, 0);

    // Show Keyboard Button (matches VR button functionality)
    showKeyboardButton = new Button(this);
    showKeyboardButton.setText("Show Keyboard");
    showKeyboardButton.setOnClickListener(v -> {
      Log.d("MainActivity", "Show Keyboard button pressed");
      focusTextView();
    });

    // Hide Keyboard Button (matches VR button functionality)
    hideKeyboardButton = new Button(this);
    hideKeyboardButton.setText("Hide Keyboard");
    hideKeyboardButton.setOnClickListener(v -> {
      Log.d("MainActivity", "Hide Keyboard button pressed");
      clearFocusTextView();
    });

    // Clear Text Button (matches VR button functionality)
    clearTextButton = new Button(this);
    clearTextButton.setText("Clear Text");
    clearTextButton.setOnClickListener(v -> {
      Log.d("MainActivity", "Clear Text button pressed");
      mScTextView.setText("");
    });

    // Add buttons to layout with equal weights
    LinearLayout.LayoutParams buttonParams = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f);
    buttonParams.setMargins(5, 0, 5, 0);

    showKeyboardButton.setLayoutParams(buttonParams);
    hideKeyboardButton.setLayoutParams(buttonParams);
    clearTextButton.setLayoutParams(buttonParams);

    buttonLayout.addView(showKeyboardButton);
    buttonLayout.addView(hideKeyboardButton);
    buttonLayout.addView(clearTextButton);

    mainLayout.addView(buttonLayout);

    // Status text
    TextView statusLabel = new TextView(this);
    statusLabel.setText("\nStatus: Ready");
    statusLabel.setTextSize(14);
    statusLabel.setPadding(0, 20, 0, 0);
    mainLayout.addView(statusLabel);

    setContentView(mainLayout);

    // Add input listeners
    mScTextView.addTextChangedListener(this);
  }

  // Method called from native code to focus the TextView and show soft keyboard
  public void focusTextView() {
      // Post with a delay to ensure the view is properly attached to the window
      mScTextView.postDelayed(
          new Runnable() {
            @Override
            public void run() {
              if (mScTextView == null) {
                return;
              }
              // Get the InputMethodManager
              if (mScTextView.requestFocus()) {
                 Log.d("MainActivity", "DEBUG Request focus true");
              } else {
                 Log.d("MainActivity", "DEBUG Request focus false");
              }
              InputMethodManager imm =
                  (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
              // Show the soft keyboard with SHOW_IMPLICIT flag (non-deprecated alternative)
              if (imm.showSoftInput(mScTextView, InputMethodManager.SHOW_IMPLICIT)) {
                Log.d("MainActivity", "DEBUG Soft keyboard is now visible");
              } else {
                Log.d("MainActivity", "DEBUG Soft keyboard is invisible");
              }
            }
          }, 300);
  }

  // Method called from native code to clear focus from TextView and hide soft keyboard
  public void clearFocusTextView() {
    if (mScTextView != null) {
      // Post to ensure proper execution on UI thread
      mScTextView.postDelayed(
          new Runnable() {
            @Override
            public void run() {
              if (mScTextView != null) {
              // Clear focus from the TextView
              mScTextView.clearFocus();
              }
            }
          }, 300);
    }
  }

  // TextWatcher interface methods
  @Override
  public void beforeTextChanged(CharSequence s, int start, int count, int after) {
    // Do nothing
  }

  @Override
  public void onTextChanged(CharSequence s, int start, int before, int count) {
    // Do nothing
  }

  @Override
  public void afterTextChanged(Editable s) {
    // Call native method to update textInputBuffer_
    updateTextInputBuffer(s.toString());
  }

  // Native method declaration - will be implemented in C++
  public native void updateTextInputBuffer(String text);

}
