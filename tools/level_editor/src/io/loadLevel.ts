import { EditorLevel, LevelData, levelDataToEditor } from '../model/Level';

export function parseLevelJson(jsonText: string): EditorLevel {
  const parsed = JSON.parse(jsonText) as LevelData;
  if (!parsed.id || !parsed.map) {
    throw new Error("Invalid level format: missing 'id' or 'map' block.");
  }
  return levelDataToEditor(parsed);
}

export function promptLoadLevelFile(): Promise<{ fileName: string; level: EditorLevel }> {
  return new Promise((resolve, reject) => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json,application/json';
    input.onchange = (e: any) => {
      const file = e.target.files?.[0];
      if (!file) return reject(new Error('No file selected'));

      const reader = new FileReader();
      reader.onload = (event) => {
        try {
          const content = event.target?.result as string;
          const level = parseLevelJson(content);
          resolve({ fileName: file.name, level });
        } catch (err) {
          reject(err);
        }
      };
      reader.onerror = () => reject(new Error('Failed to read file'));
      reader.readAsText(file);
    };
    input.click();
  });
}
