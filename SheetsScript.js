/**
 * Google Apps Script to append CSV data to a spreadsheet
 * Spreadsheet columns: Type, Value1, Value2, Value3, Value4, Value5, Value6, Value7, Value8
 */

// Configuration - Update these values for your specific spreadsheet
const SPREADSHEET_ID = '1lPdhzccMVJmedF720gXMA-sK0uFFamBP4QlcnJuI3qU'; // Replace with your actual spreadsheet ID
const SHEET_NAME = 'Sheet1'; // Replace with your actual sheet name

/**
 * Main function to append CSV data to the spreadsheet
 * @param {string} csvData - Plain text CSV data to append
 * @param {string} delimiter - CSV delimiter (default: comma)
 */
function appendCsvData(csvData, delimiter = ',') {
  try {
    // Open the spreadsheet
    const spreadsheet = SpreadsheetApp.openById(SPREADSHEET_ID);
    const sheet = spreadsheet.getSheetByName(SHEET_NAME);
    
    if (!sheet) {
      throw new Error(`Sheet "${SHEET_NAME}" not found`);
    }
    
    // Parse CSV data
    const rows = parseCsvData(csvData, delimiter);
    
    if (rows.length === 0) {
      Logger.log('No data to append');
      return 'No data to append';
    }
    
    // Validate that each row has the correct number of columns (9 total)
    const validRows = rows.filter(row => {
      if (row.length !== 9) {
        Logger.log(`Skipping row with ${row.length} columns: ${row.join(', ')}`);
        return false;
      }
      return true;
    });
    
    if (validRows.length === 0) {
      throw new Error('No valid rows found. Each row must have exactly 9 columns.');
    }
    
    // Append data to the sheet
    const lastRow = sheet.getLastRow();
    const range = sheet.getRange(lastRow + 1, 1, validRows.length, 9);
    range.setValues(validRows);
    
    Logger.log(`Successfully appended ${validRows.length} rows to ${SHEET_NAME}`);
    return `Successfully appended ${validRows.length} rows`;
    
  } catch (error) {
    Logger.log(`Error: ${error.message}`);
    throw error;
  }
}

/**
 * Parse CSV data into a 2D array
 * @param {string} csvData - Raw CSV data
 * @param {string} delimiter - CSV delimiter
 * @returns {Array[]} 2D array of parsed data
 */
function parseCsvData(csvData, delimiter) {
  if (!csvData || csvData.trim() === '') {
    return [];
  }
  
  const lines = csvData.trim().split('\n');
  const rows = [];
  
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i].trim();
    if (line === '') continue; // Skip empty lines
    
    // Simple CSV parsing (handles basic cases)
    const row = parseCSVLine(line, delimiter);
    rows.push(row);
  }
  
  return rows;
}

/**
 * Parse a single CSV line, handling quoted fields
 * @param {string} line - CSV line to parse
 * @param {string} delimiter - CSV delimiter
 * @returns {Array} Array of field values
 */
function parseCSVLine(line, delimiter) {
  const result = [];
  let current = '';
  let inQuotes = false;
  
  for (let i = 0; i < line.length; i++) {
    const char = line[i];
    
    if (char === '"') {
      if (inQuotes && line[i + 1] === '"') {
        // Handle escaped quotes
        current += '"';
        i++; // Skip next quote
      } else {
        // Toggle quote state
        inQuotes = !inQuotes;
      }
    } else if (char === delimiter && !inQuotes) {
      // Field separator found
      result.push(current.trim());
      current = '';
    } else {
      current += char;
    }
  }
  
  // Add the last field
  result.push(current.trim());
  
  return result;
}

/**
 * Test function with sample data
 * Uncomment and modify to test the script
 */
function testAppendCsvData() {
  const sampleCsvData = `TypeA,Value1A,Value2A,Value3A,Value4A,Value5A,Value6A,Value7A,Value8A
TypeB,Value1B,Value2B,Value3B,Value4B,Value5B,Value6B,Value7B,Value8B
TypeC,Value1C,Value2C,Value3C,Value4C,Value5C,Value6C,Value7C,Value8C`;
  
  try {
    const result = appendCsvData(sampleCsvData);
    Logger.log(result);
  } catch (error) {
    Logger.log(`Test failed: ${error.message}`);
  }
}

/**
 * Setup function to initialize the spreadsheet with headers
 * Run this once to set up your spreadsheet headers
 */
function setupSpreadsheetHeaders() {
  try {
    const spreadsheet = SpreadsheetApp.openById(SPREADSHEET_ID);
    const sheet = spreadsheet.getSheetByName(SHEET_NAME);
    
    if (!sheet) {
      throw new Error(`Sheet "${SHEET_NAME}" not found`);
    }
    
    // Check if headers already exist
    const firstRow = sheet.getRange(1, 1, 1, 9).getValues()[0];
    if (firstRow[0] !== '' && firstRow[0] !== 'Type') {
      Logger.log('Headers may already exist. First row contains data.');
      return 'Headers may already exist';
    }
    
    // Set headers
    const headers = ['Type', 'Value1', 'Value2', 'Value3', 'Value4', 'Value5', 'Value6', 'Value7', 'Value8'];
    sheet.getRange(1, 1, 1, 9).setValues([headers]);
    
    // Format headers
    const headerRange = sheet.getRange(1, 1, 1, 9);
    headerRange.setFontWeight('bold');
    headerRange.setBackground('#f0f0f0');
    
    Logger.log('Headers set successfully');
    return 'Headers set successfully';
    
  } catch (error) {
    Logger.log(`Error setting headers: ${error.message}`);
    throw error;
  }
}

/**
 * Web app function to handle HTTP POST requests
 * Deploy as web app to accept CSV data via HTTP
 */
function doPost(e) {
  try {
    let csvData = '';
    
    // Try different ways to extract the CSV data
    if (e.postData && e.postData.contents) {
      csvData = e.postData.contents;
    } else if (e.parameter && e.parameter.csvData) {
      csvData = e.parameter.csvData;
    } else if (e.postData && e.postData.getDataAsString) {
      csvData = e.postData.getDataAsString();
    } else {
      // Log the received data for debugging
      Logger.log('Received event object: ' + JSON.stringify(e));
      throw new Error('No CSV data found in request. Send CSV data in request body or as csvData parameter.');
    }
    
    if (!csvData || csvData.trim() === '') {
      throw new Error('CSV data is empty');
    }
    
    const result = appendCsvData(csvData);
    
    return ContentService
      .createTextOutput(JSON.stringify({ success: true, message: result }))
      .setMimeType(ContentService.MimeType.JSON);
      
  } catch (error) {
    Logger.log('doPost error: ' + error.message);
    return ContentService
      .createTextOutput(JSON.stringify({ success: false, error: error.message }))
      .setMimeType(ContentService.MimeType.JSON);
  }
}