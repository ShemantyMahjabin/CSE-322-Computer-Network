package offline;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.net.Socket;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.UUID;
public class ClientHandler extends Thread{
    private String username;
    private Socket clientSocket;
    private Server server;
    private BufferedReader reader;
    private PrintWriter writer;
    //private BufferedWriter writer;
    File clientDirectory;
    File clientDirectoryClientSide;
    private static final int MAX_CHUNK_SIZE = 1024 * 1000000000; // Maximum chunk size in bytes
    private static final int MIN_CHUNK_SIZE = 1024; 
    static final long MAX_CAPACITY = 1024L * 1024L * 1024L * 1024L;; 
    private static final String COMPLETE = "<END_OF_RESPONSE>";
    private static final String FILE_DOWNLODED = "FILE_DOWNLODED";
    public ClientHandler(Socket socket, Server server){
        this.clientSocket = socket;
        this.server = server;
    }

    @Override
    public void run(){
        try {
            reader = new BufferedReader(new InputStreamReader(clientSocket.getInputStream()));
            //writer = new BufferedWriter(new OutputStreamWriter(clientSocket.getOutputStream()));
            writer = new PrintWriter(clientSocket.getOutputStream(), true);
            System.out.println("Client connected: " + clientSocket.getInetAddress().getHostAddress());
            // if(!login()){
            //     return;
            // }
            // String message;
            // while ((message = reader.readLine()) != null) {
            //     System.out.println(username + ": " + message);
            // }
            boolean loggedIn = login();
            while (loggedIn) {
                if(!clientSocket.isConnected()){
                    //terminate hoice,loop break
                    break;
                }
                String rqst=reader.readLine();
                if(rqst!=null){
                    if(rqst.equalsIgnoreCase("1")){
                        String connecteduser=server.getConnectedUsers();
                        writer.println("Connected clients:" + connecteduser);
                        String disconnecteduser = server.getDisconnectedUsers();
                        writer.println("Disconnected clients:" + disconnecteduser);
                        writer.println(COMPLETE);
                    }
                    else if(rqst.equalsIgnoreCase("2")){
                        ownListofFiles();
                    }
                    else if(rqst.equalsIgnoreCase("3")){
                        ListOfFiles();
                    }
                    else if(rqst.equalsIgnoreCase("4")){
                        downloadFile();
                    }
                    else if(rqst.equalsIgnoreCase("5")){
                        uploadeFile();
                    }
                    else if(rqst.equalsIgnoreCase("6")){
                        String description=reader.readLine();
                        //long requestId=Long.parseLong(reader.readLine());
                        String recipient=reader.readLine();
                        //FileRequest(username,requestId,description,recipient);
                        FileRequest(username, description, recipient);
                    }
                    else if(rqst.equalsIgnoreCase("7")){
                        showOwnInbox();
                    }
                    else if(rqst.equalsIgnoreCase("9")){
                        showHistory();
                    }
                    else if (rqst.equalsIgnoreCase("8")) {
                        writer.println("Logged out successfully.");
                        writer.println(COMPLETE);
                        logout();
                        break;
                    }

                   
                    else{

                    }
                }    
        } 
    }catch (IOException e) {
        System.out.println("Connection error for client " + username);
        logout();
    }


    }
    private boolean login() throws IOException {
        writer.println("Enter username:");
        username=reader.readLine();
        if(server.isNameTaken(username,clientSocket)){
            writer.println("Username already taken. Connection closing.");
            clientSocket.close();
            return false;
        }
        if(server.isInDisconnectedList(username)){
            server.disconnectedclients.remove(username);
        }else{
            server.users.add(username);
        }
        
            server.connectedclients.put(username, clientSocket);
            //server.onlineClients.add(this);
            

            //directory part
            clientDirectory=new File("server_files/"+username);
            if (!clientDirectory.exists()) {
                if(clientDirectory.mkdirs()){
                    System.out.println("Directory created for user: " + username);
                } else {
                    System.out.println("Failed to create directory for user: " + username);
                }
            }
            clientDirectoryClientSide=new File("client_files/"+username);
            if (!clientDirectoryClientSide.exists()) {
                if(clientDirectoryClientSide.mkdirs()){
                    System.out.println("Directory created for user(client side): " + username);
                } else {
                    System.out.println("Failed to create directory for user(client side): " + username);
                }
            }
            writer.println("Login successful. Welcome, " + username + "!");
            return true;
        
    }

    private void ownListofFiles()
    {
        writer.println("Files in your directory:");
        List<String>privateFiles=server.UploadedPrivateFilesByUsers.get(username);
        List<String>publicFiles=server.UploadedPublicFilesByUsers.get(username);
        if(privateFiles!=null){
            writer.println("Private Files:");
            for(String fileName:privateFiles){
                writer.println(fileName);
            }
        }
        if(publicFiles!=null){
            writer.println("Public Files:");
            for(String fileName:publicFiles){
                writer.println(fileName);
            }
        }
        writer.println(COMPLETE);
    }

    private void ListOfFiles()
    {
        writer.println("Files uploaded by all users:");
        for(String user:server.users){
            //List<String>privateFiles=server.UploadedPrivateFilesByUsers.get(user);
            List<String>publicFiles=server.UploadedPublicFilesByUsers.get(user);
            // if(privateFiles!=null){
            //     writer.println("Private Files of "+user+":");
            //     for(String fileName:privateFiles){
            //         writer.println(fileName);
            //     }
            // }
            if(publicFiles!=null){
                writer.println("Public Files of "+user+":");
                for(String fileName:publicFiles){
                    writer.println(fileName);
                }
            }
        }
        writer.println(COMPLETE);
    }

private void downloadFile() throws IOException{
        String filename=reader.readLine();
        String fileowner=reader.readLine();
        String requestedFile=reader.readLine();
        File targetFile=new File(filename);
        boolean isallowed=false;
        if(server.UploadedPublicFilesByUsers.containsKey(fileowner)){
            isallowed=server.UploadedPublicFilesByUsers.get(fileowner).contains(requestedFile); 
        } 
        if(!isallowed && fileowner.equalsIgnoreCase(this.username)){
            if( server.UploadedPrivateFilesByUsers.containsKey(fileowner)){
                isallowed = server.UploadedPrivateFilesByUsers.get(fileowner).contains(requestedFile);
            }
        }
        if(!isallowed){
            writer.println("FILE_NOT_FOUND");
            writer.flush();
            logAction(requestedFile, "DOWNLOAD", "FAILED");
            return;
        }
        if(!targetFile.exists()){
            writer.println("FILE_NOT_FOUND");
            writer.flush();
            logAction(requestedFile, "DOWNLOAD", "FAILED");
            return;
        }
        writer.println("FILE_FOUND");
        try(BufferedInputStream in=new BufferedInputStream(new FileInputStream(targetFile))){
            byte[] buffer = new byte[MAX_CHUNK_SIZE];
            int n;
            while((n = in.read(buffer))!=-1){
                writer.println(new String(buffer, 0, n));
            }
        }
        writer.println(FILE_DOWNLODED);
        writer.flush();
        logAction(requestedFile, "DOWNLOAD", "SUCCESSFUL");

}

private void uploadeFile() throws IOException{
    String filename = reader.readLine();
    String filetype = reader.readLine(); // "public" or "private"
    String notifyOnRequest = reader.readLine(); // "y" or "n"
    String requestId = (reader.readLine()); // request ID if notifyOnRequest is "y"
    int declaredFileSize = Integer.parseInt(reader.readLine());

    if(server.currentServerSize + declaredFileSize > MAX_CAPACITY){
        writer.println("File uiplod exceeds maximum buffer size.Please try again");
        logAction(filename, "UPLOAD", "FAILED");
        return;
    }
    writer.println("yes");
    
    //random chunk
    int choosenChunk = MIN_CHUNK_SIZE + new Random().nextInt(MAX_CHUNK_SIZE-MIN_CHUNK_SIZE+1);
    writer.println(choosenChunk);
    //unique id generation
    String newFileId = UUID.randomUUID().toString();
    writer.println(newFileId);
    //client kotogula chunk pathate chay
    int expectedChunks = Integer.parseInt(reader.readLine());
    File destination = new File(clientDirectory, filename);
    boolean savedProperly = receiveBinaryStream(destination, choosenChunk, declaredFileSize, expectedChunks);

    if(!savedProperly){
        destination.delete();
        writer.println("Upload failed");
        writer.println(COMPLETE);
        logAction(filename, "UPLOAD", "FAILED");
        return;
    }
    updateFileRegistry(username, filename, filetype);
    if(notifyOnRequest.equalsIgnoreCase("y")){
        notifyFileUploadRequest(requestId, filename);
    }
    server.currentServerSize += declaredFileSize;
    writer.println("File uploaded successfully");
    writer.println(COMPLETE);
    logAction(filename, "UPLOAD", "SUCCESSFUL");
}
private boolean receiveBinaryStream(File targetFile,int chunkSize,int totalBytesExpected,int ackLimit){
    try(BufferedOutputStream fileout = new BufferedOutputStream(new FileOutputStream(targetFile))){
        InputStream in = clientSocket.getInputStream();
    
        byte[] recvBuffer = new byte[chunkSize];
        int bytesRead;
        int processed = 0;
        int ackCount = 0;
        while((bytesRead = in.read(recvBuffer)) != -1){
            fileout.write(recvBuffer, 0, bytesRead);
            fileout.flush();
            processed += bytesRead;
            
            if(ackCount < ackLimit){
                writer.println("OK");
                writer.flush();
            }
            ackCount++;
            if(processed >= totalBytesExpected){
                break;
            }

        }
        return processed >= totalBytesExpected;
    }catch(IOException e){
        e.printStackTrace();
        return false;
    }
 }


 private void updateFileRegistry(String user,String filename,String filetype){
    Map<String,List<String>> fileMap;
    if(filetype.equalsIgnoreCase("public")){
        fileMap=server.UploadedPublicFilesByUsers;
    }else{
        fileMap=server.UploadedPrivateFilesByUsers;
    }
    if(!fileMap.containsKey(user)){
        fileMap.put(user, new ArrayList<>());
    }
    fileMap.get(user).add(filename);
}

private void notifyFileUploadRequest(String requestId, String filename){
    if(!server.allRequests.containsKey(requestId)){
        writer.println("Invalid request id. But your file is uploaded publicly");
        return;
    }
    ClientHandler requester=server.allRequests.get(requestId);
    String msg = "There is a message for you\n" +
    "Sender: Server\n" +
    "One of your requested files was uploaded by: " + this.username + "\n" + 
    "Uploaded File: " + filename + "\n";
    if(!server.inbox.containsKey(requester.username)){
        server.inbox.put(requester.username, new ArrayList<>());
    }
    server.inbox.get(requester.username).add(msg);
} 

private void FileRequest(String username,String description,String recipient){
    long requestid=System.currentTimeMillis();
    System.out.println("Generated request ID: " + requestid);
    server.allRequests.put(String.valueOf(requestid), this);
    String message= "There is a message for you\n" +
    "Sender: " + username + "\n" +description + "\nRequest ID: " + requestid + "\n";
    if(recipient.equalsIgnoreCase("ALL")) {
        for(String user: server.users){
            if(!user.equalsIgnoreCase(username)){
                List<String> list = server.inbox.get(user);
                if(list == null){
                    list = new ArrayList<>();
                   //server.inbox.put(user, list);
                }
                list.add(message); 
                server.inbox.put(user, list);
            }
        }
    }
    else {
        List<String> list = server.inbox.get(recipient);
        if(list == null){
            list = new ArrayList<>();
           //erver.inbox.put(recipient, list);
        }
        list.add(message);
        server.inbox.put(recipient, list);
    }
      writer.println("File request sent successfully.");
}

private void showOwnInbox(){
    writer.println("Own Inbox:");
    List<String> messages = server.inbox.get(this.username);
    if(messages!=null){
        for(String msg:messages){
            writer.println(msg);
        }
    }
    writer.println("That's all");
    writer.println(COMPLETE);
}
private void showHistory(){
    writer.println("Upload/Download History:");
    File historyFile = new File(clientDirectory, "history.log");
    if(!historyFile.exists()){
        writer.println("No history found.");
        writer.println(COMPLETE);
        return;
    }
    try (java.io.BufferedReader br = new java.io.BufferedReader(new java.io.FileReader(historyFile))) {
        String line;
        while((line = br.readLine()) != null){
            writer.println(line);
        }
    } catch (IOException e){
        writer.println("Error reading history: " + e.getMessage());
    }
    writer.println(COMPLETE);
}
// private void logout(){
//         if(username!=null){
//             server.connectedclients.remove(username);
//             server.onlineClients.remove(this);
//             if(!server.isInDisconnectedList(username))
//             {
//                 server.disconnectedclients.put(username,clientSocket);
//             }
//             System.out.println("Client "+username+" disconnected.");
//         }
//          // clientSocket.close();
//     }

private void logout() {
    try {
        if (username != null) {
            server.connectedclients.remove(username);
            //server.onlineClients.remove(this);

            if (!server.isInDisconnectedList(username)) {
                server.disconnectedclients.put(username, clientSocket);
            }

            System.out.println("Client " + username + " disconnected.");
            writer.println(COMPLETE);
        }

        clientSocket.close();
    } catch (IOException e) {
        e.printStackTrace();
    }
}

private void logAction(String filename, String action, String status) {
    try {
        File historyFile = new File(clientDirectory, "history.log");
        java.time.LocalDateTime now = java.time.LocalDateTime.now();
        java.time.format.DateTimeFormatter formatter = java.time.format.DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");
        String timestamp = now.format(formatter);
        
        
        
        String logEntry = filename + " | " + timestamp + " | " + action + " | " + status;
        
        try (java.io.FileWriter fw = new java.io.FileWriter(historyFile, true);
             java.io.BufferedWriter bw = new java.io.BufferedWriter(fw)) {
            bw.write(logEntry);
            bw.newLine();
            bw.flush();
        }
    } catch (IOException e) {
        System.err.println("Error logging action for user " + username + ": " + e.getMessage());
    }
}

}
